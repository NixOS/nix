#include "attr-set.hh"
#include "eval.hh"
#include "eval-settings.hh"
#include "nixexpr.hh"
#include "users.hh"
#include "symbol-table.hh"

#include "change_head.hh"
#include "grammar.hh"
#include "state.hh"

#include <charconv>
#include <memory>

// flip this define when doing parser development to enable some g checks.
#if 0
#include <tao/pegtl/contrib/analyze.hpp>
#define ANALYZE_GRAMMAR \
    ([] { \
        const std::size_t issues = tao::pegtl::analyze<grammar::root>(); \
        assert(issues == 0); \
    })()
#else
#define ANALYZE_GRAMMAR ((void) 0)
#endif

namespace p = tao::pegtl;

namespace nix::parser {
namespace {

template<typename>
inline constexpr const char * error_message = nullptr;

#define error_message_for(...) \
    template<> inline constexpr auto error_message<__VA_ARGS__>

error_message_for(p::one<'{'>) = "expecting '{'";
error_message_for(p::one<'}'>) = "expecting '}'";
error_message_for(p::one<'"'>) = "expecting '\"'";
error_message_for(p::one<';'>) = "expecting ';'";
error_message_for(p::one<')'>) = "expecting ')'";
error_message_for(p::one<'='>) = "expecting '='";
error_message_for(p::one<']'>) = "expecting ']'";
error_message_for(p::one<':'>) = "expecting ':'";
error_message_for(p::string<'\'', '\''>) = "expecting \"''\"";
error_message_for(p::any) = "expecting any character";
error_message_for(grammar::eof) = "expecting end of file";
error_message_for(grammar::seps) = "expecting separators";
error_message_for(grammar::path::forbid_prefix_triple_slash) = "too many slashes in path";
error_message_for(grammar::path::forbid_prefix_double_slash_no_interp) = "path has a trailing slash";
error_message_for(grammar::expr) = "expecting expression";
error_message_for(grammar::expr::unary) = "expecting expression";
error_message_for(grammar::binding::equal) = "expecting '='";
error_message_for(grammar::expr::lambda::arg) = "expecting identifier";
error_message_for(grammar::formals) = "expecting formals";
error_message_for(grammar::attrpath) = "expecting attribute path";
error_message_for(grammar::expr::select) = "expecting selection expression";
error_message_for(grammar::t::kw_then) = "expecting 'then'";
error_message_for(grammar::t::kw_else) = "expecting 'else'";
error_message_for(grammar::t::kw_in) = "expecting 'in'";

struct SyntaxErrors
{
    template<typename Rule>
    static constexpr auto message = error_message<Rule>;

    template<typename Rule>
    static constexpr bool raise_on_failure = false;
};

template<typename Rule>
struct Control : p::must_if<SyntaxErrors>::control<Rule>
{
    template<typename ParseInput, typename... States>
    [[noreturn]] static void raise(const ParseInput & in, States &&... st)
    {
        if (in.empty()) {
            std::string expected;
            if constexpr (constexpr auto msg = error_message<Rule>)
                expected = fmt(", %s", msg);
            throw p::parse_error("unexpected end of file" + expected, in);
        }
        p::must_if<SyntaxErrors>::control<Rule>::raise(in, st...);
    }
};

struct ExprState : grammar::operator_semantics<ExprState, PosIdx, AttrPath, std::unique_ptr<Expr>>
{
    template<typename Op, typename... Args>
    std::unique_ptr<Expr> applyUnary(Args &&... args) {
        return std::make_unique<Op>(popExpr(), std::forward<Args>(args)...);
    }

    template<typename Op>
    std::unique_ptr<Expr> applyBinary(PosIdx pos) {
        auto right = popExpr(), left = popExpr();
        return std::make_unique<Op>(pos, std::move(left), std::move(right));
    }

    std::unique_ptr<Expr> call(PosIdx pos, Symbol fn, bool flip = false)
    {
        std::vector<std::unique_ptr<Expr>> args(2);
        args[flip ? 0 : 1] = popExpr();
        args[flip ? 1 : 0] = popExpr();
        return std::make_unique<ExprCall>(pos, std::make_unique<ExprVar>(fn), std::move(args));
    }

    std::unique_ptr<Expr> order(PosIdx pos, bool less, State & state)
    {
        return call(pos, state.s.lessThan, !less);
    }

    std::unique_ptr<Expr> concatStrings(PosIdx pos)
    {
        std::vector<std::pair<PosIdx, std::unique_ptr<Expr>>> args(2);
        args[1].second = popExpr();
        args[0].second = popExpr();
        return std::make_unique<ExprConcatStrings>(pos, false, std::move(args));
    }

    std::unique_ptr<Expr> negate(PosIdx pos, State & state)
    {
        std::vector<std::unique_ptr<Expr>> args(2);
        args[0] = std::make_unique<ExprInt>(0);
        args[1] = popExpr();
        return std::make_unique<ExprCall>(pos, std::make_unique<ExprVar>(state.s.sub), std::move(args));
    }

    std::unique_ptr<Expr> applyOp(PosIdx pos, auto & op, State & state) {
        using Op = grammar::op;

        auto not_ = [] (auto e) {
            return std::make_unique<ExprOpNot>(std::move(e));
        };

        return (overloaded {
            [&] (Op::implies)     { return applyBinary<ExprOpImpl>(pos); },
            [&] (Op::or_)         { return applyBinary<ExprOpOr>(pos); },
            [&] (Op::and_)        { return applyBinary<ExprOpAnd>(pos); },
            [&] (Op::equals)      { return applyBinary<ExprOpEq>(pos); },
            [&] (Op::not_equals)  { return applyBinary<ExprOpNEq>(pos); },
            [&] (Op::less)        { return order(pos, true, state); },
            [&] (Op::greater_eq)  { return not_(order(pos, true, state)); },
            [&] (Op::greater)     { return order(pos, false, state); },
            [&] (Op::less_eq)     { return not_(order(pos, false, state)); },
            [&] (Op::update)      { return applyBinary<ExprOpUpdate>(pos); },
            [&] (Op::not_)        { return applyUnary<ExprOpNot>(); },
            [&] (Op::plus)        { return concatStrings(pos); },
            [&] (Op::minus)       { return call(pos, state.s.sub); },
            [&] (Op::mul)         { return call(pos, state.s.mul); },
            [&] (Op::div)         { return call(pos, state.s.div); },
            [&] (Op::concat)      { return applyBinary<ExprOpConcatLists>(pos); },
            [&] (has_attr & a)    { return applyUnary<ExprOpHasAttr>(std::move(a.path)); },
            [&] (Op::unary_minus) { return negate(pos, state); },
        })(op);
    }

    // always_inline is needed, otherwise pushOp slows down considerably
    [[noreturn, gnu::always_inline]]
    static void badOperator(PosIdx pos, State & state)
    {
        throw ParseError({
            .msg = HintFmt("syntax error, unexpected operator"),
            .pos = state.positions[pos]
        });
    }

    template<typename Expr, typename... Args>
    Expr & pushExpr(Args && ... args)
    {
        auto p = std::make_unique<Expr>(std::forward<Args>(args)...);
        auto & result = *p;
        exprs.emplace_back(std::move(p));
        return result;
    }
};

struct SubexprState {
private:
    ExprState * up;

public:
    explicit SubexprState(ExprState & up, auto &...) : up(&up) {}
    operator ExprState &() { return *up; }
    ExprState * operator->() { return up; }
};



template<typename Rule>
struct BuildAST : grammar::nothing<Rule> {};

struct LambdaState : SubexprState {
    using SubexprState::SubexprState;

    Symbol arg;
    std::unique_ptr<Formals> formals;
};

struct FormalsState : SubexprState {
    using SubexprState::SubexprState;

    Formals formals{};
    Formal formal{};
};

template<> struct BuildAST<grammar::formal::name> {
    static void apply(const auto & in, FormalsState & s, State & ps) {
        s.formal = {
            .pos = ps.at(in),
            .name = ps.symbols.create(in.string_view()),
        };
    }
};

template<> struct BuildAST<grammar::formal> {
    static void apply0(FormalsState & s, State &) {
        s.formals.formals.emplace_back(std::move(s.formal));
    }
};

template<> struct BuildAST<grammar::formal::default_value> {
    static void apply0(FormalsState & s, State & ps) {
        s.formal.def = s->popExpr();
    }
};

template<> struct BuildAST<grammar::formals::ellipsis> {
    static void apply0(FormalsState & s, State &) {
        s.formals.ellipsis = true;
    }
};

template<> struct BuildAST<grammar::formals> : change_head<FormalsState> {
    static void success0(FormalsState & f, LambdaState & s, State &) {
        s.formals = std::make_unique<Formals>(std::move(f.formals));
    }
};

struct AttrState : SubexprState {
    using SubexprState::SubexprState;

    std::vector<AttrName> attrs;

    void pushAttr(auto && attr, PosIdx) { attrs.emplace_back(std::move(attr)); }
};

template<> struct BuildAST<grammar::attr::simple> {
    static void apply(const auto & in, auto & s, State & ps) {
        s.pushAttr(ps.symbols.create(in.string_view()), ps.at(in));
    }
};

template<> struct BuildAST<grammar::attr::string> {
    static void apply(const auto & in, auto & s, State & ps) {
        auto e = s->popExpr();
        if (auto str = dynamic_cast<ExprString *>(e.get()))
            s.pushAttr(ps.symbols.create(str->s), ps.at(in));
        else
            s.pushAttr(std::move(e), ps.at(in));
    }
};

template<> struct BuildAST<grammar::attr::expr> : BuildAST<grammar::attr::string> {};

struct BindingsState : SubexprState {
    using SubexprState::SubexprState;

    ExprAttrs attrs;
    AttrPath path;
    std::unique_ptr<Expr> value;
};

struct InheritState : SubexprState {
    using SubexprState::SubexprState;

    std::vector<std::pair<AttrName, PosIdx>> attrs;
    std::unique_ptr<Expr> from;
    PosIdx fromPos;

    void pushAttr(auto && attr, PosIdx pos) { attrs.emplace_back(std::move(attr), pos); }
};

template<> struct BuildAST<grammar::inherit::from> {
    static void apply(const auto & in, InheritState & s, State & ps) {
        s.from = s->popExpr();
        s.fromPos = ps.at(in);
    }
};

template<> struct BuildAST<grammar::inherit> : change_head<InheritState> {
    static void success0(InheritState & s, BindingsState & b, State & ps) {
        auto & attrs = b.attrs.attrs;
        // TODO this should not reuse generic attrpath rules.
        for (auto & [i, iPos] : s.attrs) {
            if (i.symbol)
                continue;
            if (auto str = dynamic_cast<ExprString *>(i.expr.get()))
                i = AttrName(ps.symbols.create(str->s));
            else {
                throw ParseError({
                    .msg = HintFmt("dynamic attributes not allowed in inherit"),
                    .pos = ps.positions[iPos]
                });
            }
        }
        if (auto fromE = std::move(s.from)) {
            if (!b.attrs.inheritFromExprs)
                b.attrs.inheritFromExprs = std::make_unique<std::vector<std::unique_ptr<Expr>>>();
            b.attrs.inheritFromExprs->push_back(std::move(fromE));
            for (auto & [i, iPos] : s.attrs) {
                if (attrs.find(i.symbol) != attrs.end())
                    ps.dupAttr(i.symbol, iPos, attrs[i.symbol].pos);
                auto from = std::make_unique<ExprInheritFrom>(s.fromPos, b.attrs.inheritFromExprs->size() - 1);
                attrs.emplace(
                    i.symbol,
                    ExprAttrs::AttrDef(
                        std::make_unique<ExprSelect>(iPos, std::move(from), i.symbol),
                        iPos,
                        ExprAttrs::AttrDef::Kind::InheritedFrom));
            }
        } else {
            for (auto & [i, iPos] : s.attrs) {
                if (attrs.find(i.symbol) != attrs.end())
                    ps.dupAttr(i.symbol, iPos, attrs[i.symbol].pos);
                attrs.emplace(
                    i.symbol,
                    ExprAttrs::AttrDef(
                        std::make_unique<ExprVar>(iPos, i.symbol),
                        iPos,
                        ExprAttrs::AttrDef::Kind::Inherited));
            }
        }
    }
};

template<> struct BuildAST<grammar::binding::path> : change_head<AttrState> {
    static void success0(AttrState & a, BindingsState & s, State & ps) {
        s.path = std::move(a.attrs);
    }
};

template<> struct BuildAST<grammar::binding::value> {
    static void apply0(BindingsState & s, State & ps) {
        s.value = s->popExpr();
    }
};

template<> struct BuildAST<grammar::binding> {
    static void apply(const auto & in, BindingsState & s, State & ps) {
        ps.addAttr(&s.attrs, std::move(s.path), std::move(s.value), ps.at(in));
    }
};

template<> struct BuildAST<grammar::expr::id> {
    static void apply(const auto & in, ExprState & s, State & ps) {
        if (in.string_view() == "__curPos")
            s.pushExpr<ExprPos>(ps.at(in));
        else
            s.pushExpr<ExprVar>(ps.at(in), ps.symbols.create(in.string_view()));
    }
};

template<> struct BuildAST<grammar::expr::int_> {
    static void apply(const auto & in, ExprState & s, State & ps) {
        int64_t v;
        if (std::from_chars(in.begin(), in.end(), v).ec != std::errc{}) {
            throw ParseError({
                .msg = HintFmt("invalid integer '%1%'", in.string_view()),
                .pos = ps.positions[ps.at(in)],
            });
        }
        s.pushExpr<ExprInt>(v);
    }
};

template<> struct BuildAST<grammar::expr::float_> {
    static void apply(const auto & in, ExprState & s, State & ps) {
        double v;
        if (std::from_chars(in.begin(), in.end(), v).ec != std::errc{}) {
            throw ParseError({
                .msg = HintFmt("invalid float '%1%'", in.string_view()),
                .pos = ps.positions[ps.at(in)],
            });
        }
        s.pushExpr<ExprFloat>(v);
    }
};

struct StringState : SubexprState {
    using SubexprState::SubexprState;

    std::string currentLiteral;
    PosIdx currentPos;
    std::vector<std::pair<nix::PosIdx, std::unique_ptr<Expr>>> parts;

    void append(PosIdx pos, std::string_view s)
    {
        if (currentLiteral.empty())
            currentPos = pos;
        currentLiteral += s;
    }

    // FIXME this truncates strings on NUL for compat with the old parser. ideally
    // we should use the decomposition the g gives us instead of iterating over
    // the entire string again.
    static void unescapeStr(std::string & str)
    {
        char * s = str.data();
        char * t = s;
        char c;
        while ((c = *s++)) {
            if (c == '\\') {
                c = *s++;
                if (c == 'n') *t = '\n';
                else if (c == 'r') *t = '\r';
                else if (c == 't') *t = '\t';
                else *t = c;
            }
            else if (c == '\r') {
                /* Normalise CR and CR/LF into LF. */
                *t = '\n';
                if (*s == '\n') s++; /* cr/lf */
            }
            else *t = c;
            t++;
        }
        str.resize(t - str.data());
    }

    void endLiteral()
    {
        if (!currentLiteral.empty()) {
            unescapeStr(currentLiteral);
            parts.emplace_back(currentPos, std::make_unique<ExprString>(std::move(currentLiteral)));
        }
    }

    std::unique_ptr<Expr> finish()
    {
        if (parts.empty()) {
            unescapeStr(currentLiteral);
            return std::make_unique<ExprString>(std::move(currentLiteral));
        } else {
            endLiteral();
            auto pos = parts[0].first;
            return std::make_unique<ExprConcatStrings>(pos, true, std::move(parts));
        }
    }
};

template<typename... Content> struct BuildAST<grammar::string::literal<Content...>> {
    static void apply(const auto & in, StringState & s, State & ps) {
        s.append(ps.at(in), in.string_view());
    }
};

template<> struct BuildAST<grammar::string::cr_lf> {
    static void apply(const auto & in, StringState & s, State & ps) {
        s.append(ps.at(in), in.string_view()); // FIXME compat with old parser
    }
};

template<> struct BuildAST<grammar::string::interpolation> {
    static void apply(const auto & in, StringState & s, State & ps) {
        s.endLiteral();
        s.parts.emplace_back(ps.at(in), s->popExpr());
    }
};

template<> struct BuildAST<grammar::string::escape> {
    static void apply(const auto & in, StringState & s, State & ps) {
        s.append(ps.at(in), "\\"); // FIXME compat with old parser
        s.append(ps.at(in), in.string_view());
    }
};

template<> struct BuildAST<grammar::string> : change_head<StringState> {
    static void success0(StringState & s, ExprState & e, State &) {
        e.exprs.push_back(s.finish());
    }
};

struct IndStringState : SubexprState {
    using SubexprState::SubexprState;

    std::vector<std::pair<PosIdx, std::variant<std::unique_ptr<Expr>, StringToken>>> parts;
};

template<bool Indented, typename... Content>
struct BuildAST<grammar::ind_string::literal<Indented, Content...>> {
    static void apply(const auto & in, IndStringState & s, State & ps) {
        s.parts.emplace_back(ps.at(in), StringToken{in.string_view(), Indented});
    }
};

template<> struct BuildAST<grammar::ind_string::interpolation> {
    static void apply(const auto & in, IndStringState & s, State & ps) {
        s.parts.emplace_back(ps.at(in), s->popExpr());
    }
};

template<> struct BuildAST<grammar::ind_string::escape> {
    static void apply(const auto & in, IndStringState & s, State & ps) {
        switch (*in.begin()) {
        case 'n': s.parts.emplace_back(ps.at(in), StringToken{"\n"}); break;
        case 'r': s.parts.emplace_back(ps.at(in), StringToken{"\r"}); break;
        case 't': s.parts.emplace_back(ps.at(in), StringToken{"\t"}); break;
        default:  s.parts.emplace_back(ps.at(in), StringToken{in.string_view()}); break;
        }
    }
};

template<> struct BuildAST<grammar::ind_string> : change_head<IndStringState> {
    static void success(const auto & in, IndStringState & s, ExprState & e, State & ps) {
        e.exprs.emplace_back(ps.stripIndentation(ps.at(in), std::move(s.parts)));
    }
};

template<typename... Content> struct BuildAST<grammar::path::literal<Content...>> {
    static void apply(const auto & in, StringState & s, State & ps) {
        s.append(ps.at(in), in.string_view());
        s.endLiteral();
    }
};

template<> struct BuildAST<grammar::path::interpolation> : BuildAST<grammar::string::interpolation> {};

template<> struct BuildAST<grammar::path::anchor> {
    static void apply(const auto & in, StringState & s, State & ps) {
        Path path(absPath(in.string_view(), ps.basePath.path.abs()));
        /* add back in the trailing '/' to the first segment */
        if (in.string_view().ends_with('/') && in.size() > 1)
            path += "/";
        s.parts.emplace_back(ps.at(in), new ExprPath(ps.rootFS, std::move(path)));
    }
};

template<> struct BuildAST<grammar::path::home_anchor> {
    static void apply(const auto & in, StringState & s, State & ps) {
        if (evalSettings.pureEval)
            throw Error("the path '%s' can not be resolved in pure mode", in.string_view());
        Path path(getHome() + in.string_view().substr(1));
        s.parts.emplace_back(ps.at(in), new ExprPath(ps.rootFS, std::move(path)));
    }
};

template<> struct BuildAST<grammar::path::searched_path> {
    static void apply(const auto & in, StringState & s, State & ps) {
        std::vector<std::unique_ptr<Expr>> args{2};
        args[0] = std::make_unique<ExprVar>(ps.s.nixPath);
        args[1] = std::make_unique<ExprString>(in.string());
        s.parts.emplace_back(
            ps.at(in),
            std::make_unique<ExprCall>(
                ps.at(in),
                std::make_unique<ExprVar>(ps.s.findFile),
                std::move(args)));
    }
};

template<> struct BuildAST<grammar::path> : change_head<StringState> {
    template<typename E>
    static void check_slash(PosIdx end, StringState & s, State & ps) {
        auto e = dynamic_cast<E *>(s.parts.back().second.get());
        if (!e || !e->s.ends_with('/'))
            return;
        if (s.parts.size() > 1 || e->s != "/")
            throw ParseError({
                .msg = HintFmt("path has a trailing slash"),
                .pos = ps.positions[end],
            });
    }

    static void success(const auto & in, StringState & s, ExprState & e, State & ps) {
        s.endLiteral();
        check_slash<ExprPath>(ps.atEnd(in), s, ps);
        check_slash<ExprString>(ps.atEnd(in), s, ps);
        if (s.parts.size() == 1) {
            e.exprs.emplace_back(std::move(s.parts.back().second));
        } else {
            e.pushExpr<ExprConcatStrings>(ps.at(in), false, std::move(s.parts));
        }
    }
};

// strings and paths sare handled fully by the grammar-level rule for now
template<> struct BuildAST<grammar::expr::string> : p::maybe_nothing {};
template<> struct BuildAST<grammar::expr::ind_string> : p::maybe_nothing {};
template<> struct BuildAST<grammar::expr::path> : p::maybe_nothing {};

template<> struct BuildAST<grammar::expr::uri> {
    static void apply(const auto & in, ExprState & s, State & ps) {
       static bool noURLLiterals = experimentalFeatureSettings.isEnabled(Xp::NoUrlLiterals);
       if (noURLLiterals)
           throw ParseError({
               .msg = HintFmt("URL literals are disabled"),
               .pos = ps.positions[ps.at(in)]
           });
       s.pushExpr<ExprString>(in.string());
    }
};

template<> struct BuildAST<grammar::expr::ancient_let> : change_head<BindingsState> {
    static void success(const auto & in, BindingsState & b, ExprState & s, State & ps) {
        b.attrs.pos = ps.at(in);
        b.attrs.recursive = true;
        s.pushExpr<ExprSelect>(b.attrs.pos, std::make_unique<ExprAttrs>(std::move(b.attrs)), ps.s.body);
    }
};

template<> struct BuildAST<grammar::expr::rec_set> : change_head<BindingsState> {
    static void success(const auto & in, BindingsState & b, ExprState & s, State & ps) {
        b.attrs.pos = ps.at(in);
        b.attrs.recursive = true;
        s.pushExpr<ExprAttrs>(std::move(b.attrs));
    }
};

template<> struct BuildAST<grammar::expr::set> : change_head<BindingsState> {
    static void success(const auto & in, BindingsState & b, ExprState & s, State & ps) {
        b.attrs.pos = ps.at(in);
        s.pushExpr<ExprAttrs>(std::move(b.attrs));
    }
};

using ListState = std::vector<std::unique_ptr<Expr>>;

template<> struct BuildAST<grammar::expr::list> : change_head<ListState> {
    static void success0(ListState & ls, ExprState & s, State &) {
        auto e = std::make_unique<ExprList>();
        e->elems = std::move(ls);
        s.exprs.push_back(std::move(e));
    }
};

template<> struct BuildAST<grammar::expr::list::entry> : change_head<ExprState> {
    static void success0(ExprState & e, ListState & s, State & ps) {
        s.emplace_back(e.finish(ps));
    }
};

struct SelectState : SubexprState {
    using SubexprState::SubexprState;

    PosIdx pos;
    ExprSelect * e = nullptr;
};

template<> struct BuildAST<grammar::expr::select::head> {
    static void apply(const auto & in, SelectState & s, State & ps) {
        s.pos = ps.at(in);
    }
};

template<> struct BuildAST<grammar::expr::select::attr> : change_head<AttrState> {
    static void success0(AttrState & a, SelectState & s, State &) {
        s.e = &s->pushExpr<ExprSelect>(s.pos, s->popExpr(), std::move(a.attrs), nullptr);
    }
};

template<> struct BuildAST<grammar::expr::select::attr_or> {
    static void apply0(SelectState & s, State &) {
        s.e->def = s->popExpr();
    }
};

template<> struct BuildAST<grammar::expr::select::as_app_or> {
    static void apply(const auto & in, SelectState & s, State & ps) {
        std::vector<std::unique_ptr<Expr>> args(1);
        args[0] = std::make_unique<ExprVar>(ps.at(in), ps.s.or_);
        s->pushExpr<ExprCall>(s.pos, s->popExpr(), std::move(args));
    }
};

template<> struct BuildAST<grammar::expr::select> : change_head<SelectState> {
    static void success0(const auto &...) {}
};

struct AppState : SubexprState {
    using SubexprState::SubexprState;

    PosIdx pos;
    ExprCall * e = nullptr;
};

template<> struct BuildAST<grammar::expr::app::select_or_fn> {
    static void apply(const auto & in, AppState & s, State & ps) {
        s.pos = ps.at(in);
    }
};

template<> struct BuildAST<grammar::expr::app::first_arg> {
    static void apply(auto & in, AppState & s, State & ps) {
        auto arg = s->popExpr(), fn = s->popExpr();
        if ((s.e = dynamic_cast<ExprCall *>(fn.get()))) {
            // TODO remove.
            // AST compat with old parser, semantics are the same.
            // this can happen on occasions such as `<p> <p>` or `a or b or`,
            // neither of which are super worth optimizing.
            s.e->args.push_back(std::move(arg));
            s->exprs.emplace_back(std::move(fn));
        } else {
            std::vector<std::unique_ptr<Expr>> args{1};
            args[0] = std::move(arg);
            s.e = &s->pushExpr<ExprCall>(s.pos, std::move(fn), std::move(args));
        }
    }
};

template<> struct BuildAST<grammar::expr::app::another_arg> {
    static void apply0(AppState & s, State & ps) {
        s.e->args.push_back(s->popExpr());
    }
};

template<> struct BuildAST<grammar::expr::app> : change_head<AppState> {
    static void success0(const auto &...) {}
};

template<typename Op> struct BuildAST<grammar::expr::operator_<Op>> {
    static void apply(const auto & in, ExprState & s, State & ps) {
        s.pushOp(ps.at(in), Op{}, ps);
    }
};
template<> struct BuildAST<grammar::expr::operator_<grammar::op::has_attr>> : change_head<AttrState> {
    static void success(const auto & in, AttrState & a, ExprState & s, State & ps) {
        s.pushOp(ps.at(in), ExprState::has_attr{{}, std::move(a.attrs)}, ps);
    }
};

template<> struct BuildAST<grammar::expr::lambda::arg> {
    static void apply(const auto & in, LambdaState & s, State & ps) {
        s.arg = ps.symbols.create(in.string_view());
    }
};

template<> struct BuildAST<grammar::expr::lambda> : change_head<LambdaState> {
    static void success(const auto & in, LambdaState & l, ExprState & s, State & ps) {
        if (l.formals)
            l.formals = ps.validateFormals(std::move(l.formals), ps.at(in), l.arg);
        s.pushExpr<ExprLambda>(ps.at(in), l.arg, std::move(l.formals), l->popExpr());
    }
};

template<> struct BuildAST<grammar::expr::assert_> {
    static void apply(const auto & in, ExprState & s, State & ps) {
        auto body = s.popExpr(), cond = s.popExpr();
        s.pushExpr<ExprAssert>(ps.at(in), std::move(cond), std::move(body));
    }
};

template<> struct BuildAST<grammar::expr::with> {
    static void apply(const auto & in, ExprState & s, State & ps) {
        auto body = s.popExpr(), scope = s.popExpr();
        s.pushExpr<ExprWith>(ps.at(in), std::move(scope), std::move(body));
    }
};

template<> struct BuildAST<grammar::expr::let> : change_head<BindingsState> {
    static void success(const auto & in, BindingsState & b, ExprState & s, State & ps) {
        if (!b.attrs.dynamicAttrs.empty())
            throw ParseError({
                .msg = HintFmt("dynamic attributes not allowed in let"),
                .pos = ps.positions[ps.at(in)]
            });

        s.pushExpr<ExprLet>(std::make_unique<ExprAttrs>(std::move(b.attrs)), b->popExpr());
    }
};

template<> struct BuildAST<grammar::expr::if_> {
    static void apply(const auto & in, ExprState & s, State & ps) {
        auto else_ = s.popExpr(), then = s.popExpr(), cond = s.popExpr();
        s.pushExpr<ExprIf>(ps.at(in), std::move(cond), std::move(then), std::move(else_));
    }
};

template<> struct BuildAST<grammar::expr> : change_head<ExprState> {
    static void success0(ExprState & inner, ExprState & outer, State & ps) {
        outer.exprs.push_back(inner.finish(ps));
    }
};

}
}

namespace nix {

Expr * EvalState::parse(
    char * text,
    size_t length,
    Pos::Origin origin,
    const SourcePath & basePath,
    std::shared_ptr<StaticEnv> & staticEnv)
{
    parser::State s = {
        symbols,
        positions,
        basePath,
        positions.addOrigin(origin, length),
        rootFS,
        exprSymbols,
    };
    parser::ExprState x;

    assert(length >= 2);
    assert(text[length - 1] == 0);
    assert(text[length - 2] == 0);
    length -= 2;

    p::string_input<p::tracking_mode::lazy> inp{std::string_view{text, length}, "input"};
    try {
        p::parse<parser::grammar::root, parser::BuildAST, parser::Control>(inp, x, s);
    } catch (p::parse_error & e) {
        auto pos = e.positions().back();
        throw ParseError({
            .msg = HintFmt("syntax error, %s", e.message()),
            .pos = positions[s.positions.add(s.origin, pos.byte)]
        });
    }

    auto result = x.finish(s);
    result->bindVars(*this, staticEnv);
    return result.release();
}

}
