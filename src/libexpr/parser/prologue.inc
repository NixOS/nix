#include "parser-tab.hh"
#include "lexer-tab.hh"

YY_DECL;

using namespace nix;

namespace nix {

static void dupAttr(const EvalState & state, const AttrPath & attrPath, const PosIdx pos, const PosIdx prevPos)
{
    throw ParseError(
        {.msg = hintfmt(
             "attribute '%1%' already defined at %2%", showAttrPath(state.symbols, attrPath), state.positions[prevPos]),
         .errPos = state.positions[pos]});
}

static void dupAttr(const EvalState & state, Symbol attr, const PosIdx pos, const PosIdx prevPos)
{
    throw ParseError(
        {.msg = hintfmt("attribute '%1%' already defined at %2%", state.symbols[attr], state.positions[prevPos]),
         .errPos = state.positions[pos]});
}

static void addAttr(ExprAttrs * attrs, AttrPath && attrPath, Expr * e, const PosIdx pos, const nix::EvalState & state)
{
    AttrPath::iterator i;
    // All attrpaths have at least one attr
    assert(!attrPath.empty());
    // Checking attrPath validity.
    // ===========================
    for (i = attrPath.begin(); i + 1 < attrPath.end(); i++) {
        if (i->symbol) {
            auto j = attrs->attrs.find(i->symbol);
            if (j != attrs->attrs.end()) {
                if (!j->second.inherited) {
                    auto * attrs2 = dynamic_cast<ExprAttrs *>(j->second.e);
                    if (!attrs2)
                        dupAttr(state, attrPath, pos, j->second.pos);
                    attrs = attrs2;
                } else
                    dupAttr(state, attrPath, pos, j->second.pos);
            } else {
                auto * nested = new ExprAttrs;
                attrs->attrs[i->symbol] = ExprAttrs::AttrDef(nested, pos);
                attrs = nested;
            }
        } else {
            auto * nested = new ExprAttrs;
            attrs->dynamicAttrs.push_back(ExprAttrs::DynamicAttrDef(i->expr, nested, pos));
            attrs = nested;
        }
    }
    // Expr insertion.
    // ==========================
    if (i->symbol) {
        auto j = attrs->attrs.find(i->symbol);
        if (j != attrs->attrs.end()) {
            // This attr path is already defined. However, if both
            // e and the expr pointed by the attr path are two attribute sets,
            // we want to merge them.
            // Otherwise, throw an error.
            auto * ae = dynamic_cast<ExprAttrs *>(e);
            auto * jAttrs = dynamic_cast<ExprAttrs *>(j->second.e);
            if (jAttrs && ae) {
                for (auto & ad : ae->attrs) {
                    auto j2 = jAttrs->attrs.find(ad.first);
                    if (j2 != jAttrs->attrs.end()) // Attr already defined in iAttrs, error.
                        dupAttr(state, ad.first, j2->second.pos, ad.second.pos);
                    jAttrs->attrs.emplace(ad.first, ad.second);
                }
                jAttrs->dynamicAttrs.insert(
                    jAttrs->dynamicAttrs.end(), ae->dynamicAttrs.begin(), ae->dynamicAttrs.end());
            } else {
                dupAttr(state, attrPath, pos, j->second.pos);
            }
        } else {
            // This attr path is not defined. Let's create it.
            attrs->attrs.emplace(i->symbol, ExprAttrs::AttrDef(e, pos));
            e->setName(i->symbol);
        }
    } else {
        attrs->dynamicAttrs.push_back(ExprAttrs::DynamicAttrDef(i->expr, e, pos));
    }
}

static Formals * toFormals(ParseData & data, ParserFormals * formals, PosIdx pos = noPos, Symbol arg = {})
{
    std::sort(formals->formals.begin(), formals->formals.end(), [](const auto & a, const auto & b) {
        return std::tie(a.name, a.pos) < std::tie(b.name, b.pos);
    });

    std::optional<std::pair<Symbol, PosIdx>> duplicate;
    for (size_t i = 0; i + 1 < formals->formals.size(); i++) {
        if (formals->formals[i].name != formals->formals[i + 1].name)
            continue;
        std::pair thisDup{formals->formals[i].name, formals->formals[i + 1].pos};
        duplicate = std::min(thisDup, duplicate.value_or(thisDup));
    }
    if (duplicate)
        throw ParseError(
            {.msg = hintfmt("duplicate formal function argument '%1%'", data.symbols[duplicate->first]),
             .errPos = data.state.positions[duplicate->second]});

    Formals result;
    result.ellipsis = formals->ellipsis;
    result.formals = std::move(formals->formals);

    if (arg && result.has(arg))
        throw ParseError(
            {.msg = hintfmt("duplicate formal function argument '%1%'", data.symbols[arg]),
             .errPos = data.state.positions[pos]});

    delete formals;
    return new Formals(std::move(result));
}

static Expr * stripIndentation(
    const PosIdx pos, SymbolTable & symbols, std::vector<std::pair<PosIdx, std::variant<Expr *, StringToken>>> && es)
{
    if (es.empty())
        return new ExprString("");

    /* Figure out the minimum indentation.  Note that by design
       whitespace-only final lines are not taken into account.  (So
       the " " in "\n ''" is ignored, but the " " in "\n foo''" is.) */
    bool atStartOfLine = true; /* = seen only whitespace in the current line */
    size_t minIndent = 1000000;
    size_t curIndent = 0;
    for (auto & [i_pos, i] : es) {
        auto * str = std::get_if<StringToken>(&i);
        if (!str || !str->hasIndentation) {
            /* Anti-quotations and escaped characters end the current start-of-line whitespace. */
            if (atStartOfLine) {
                atStartOfLine = false;
                if (curIndent < minIndent)
                    minIndent = curIndent;
            }
            continue;
        }
        for (size_t j = 0; j < str->l; ++j) {
            if (atStartOfLine) {
                if (str->p[j] == ' ')
                    curIndent++;
                else if (str->p[j] == '\n') {
                    /* Empty line, doesn't influence minimum
                       indentation. */
                    curIndent = 0;
                } else {
                    atStartOfLine = false;
                    if (curIndent < minIndent)
                        minIndent = curIndent;
                }
            } else if (str->p[j] == '\n') {
                atStartOfLine = true;
                curIndent = 0;
            }
        }
    }

    /* Strip spaces from each line. */
    auto * es2 = new std::vector<std::pair<PosIdx, Expr *>>;
    atStartOfLine = true;
    size_t curDropped = 0;
    size_t n = es.size();
    auto i = es.begin();
    const auto trimExpr = [&](Expr * e) {
        atStartOfLine = false;
        curDropped = 0;
        es2->emplace_back(i->first, e);
    };
    const auto trimString = [&](const StringToken & t) {
        std::string s2;
        for (size_t j = 0; j < t.l; ++j) {
            if (atStartOfLine) {
                if (t.p[j] == ' ') {
                    if (curDropped++ >= minIndent)
                        s2 += t.p[j];
                } else if (t.p[j] == '\n') {
                    curDropped = 0;
                    s2 += t.p[j];
                } else {
                    atStartOfLine = false;
                    curDropped = 0;
                    s2 += t.p[j];
                }
            } else {
                s2 += t.p[j];
                if (t.p[j] == '\n')
                    atStartOfLine = true;
            }
        }

        /* Remove the last line if it is empty and consists only of
           spaces. */
        if (n == 1) {
            std::string::size_type p = s2.find_last_of('\n');
            if (p != std::string::npos && s2.find_first_not_of(' ', p + 1) == std::string::npos)
                s2 = std::string(s2, 0, p + 1);
        }

        es2->emplace_back(i->first, new ExprString(std::move(s2)));
    };
    for (; i != es.end(); ++i, --n) {
        std::visit(overloaded{trimExpr, trimString}, i->second);
    }

    /* If this is a single string, then don't do a concatenation. */
    if (es2->size() == 1 && dynamic_cast<ExprString *>((*es2)[0].second)) {
        auto * const result = (*es2)[0].second;
        delete es2;
        return result;
    }
    return new ExprConcatStrings(pos, true, es2);
}

static inline PosIdx makeCurPos(const YYLTYPE & loc, ParseData * data)
{
    return data->state.positions.add(data->origin, loc.first_line, loc.first_column);
}

#define CUR_POS makeCurPos(*yylocp, data)

} // namespace nix

void yyerror(YYLTYPE * loc, yyscan_t scanner, ParseData * data, const char * error)
{
    data->error = {.msg = hintfmt(error), .errPos = data->state.positions[makeCurPos(*loc, data)]};
}
