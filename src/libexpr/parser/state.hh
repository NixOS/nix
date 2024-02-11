#pragma once
///@file

#include "eval.hh"

namespace nix::parser {

struct StringToken
{
    std::string_view s;
    bool hasIndentation;
    operator std::string_view() const { return s; }
};

struct State
{
    SymbolTable & symbols;
    PosTable & positions;
    SourcePath basePath;
    PosTable::Origin origin;
    const ref<InputAccessor> rootFS;
    const Expr::AstSymbols & s;

    void dupAttr(const AttrPath & attrPath, const PosIdx pos, const PosIdx prevPos);
    void dupAttr(Symbol attr, const PosIdx pos, const PosIdx prevPos);
    void addAttr(ExprAttrs * attrs, AttrPath && attrPath, std::unique_ptr<Expr> e, const PosIdx pos);
    std::unique_ptr<Formals> validateFormals(std::unique_ptr<Formals> formals, PosIdx pos = noPos, Symbol arg = {});
    std::unique_ptr<Expr> stripIndentation(const PosIdx pos,
        std::vector<std::pair<PosIdx, std::variant<std::unique_ptr<Expr>, StringToken>>> && es);

    // lazy positioning means we don't get byte offsets directly, in.position() would work
    // but also requires line and column (which is expensive)
    PosIdx at(const auto & in)
    {
        return positions.add(origin, in.begin() - in.input().begin());
    }

    PosIdx atEnd(const auto & in)
    {
        return positions.add(origin, in.end() - in.input().begin());
    }
};

inline void State::dupAttr(const AttrPath & attrPath, const PosIdx pos, const PosIdx prevPos)
{
    throw ParseError({
         .msg = HintFmt("attribute '%1%' already defined at %2%",
             showAttrPath(symbols, attrPath), positions[prevPos]),
         .pos = positions[pos]
    });
}

inline void State::dupAttr(Symbol attr, const PosIdx pos, const PosIdx prevPos)
{
    throw ParseError({
        .msg = HintFmt("attribute '%1%' already defined at %2%", symbols[attr], positions[prevPos]),
        .pos = positions[pos]
    });
}

inline void State::addAttr(ExprAttrs * attrs, AttrPath && attrPath, std::unique_ptr<Expr> e, const PosIdx pos)
{
    AttrPath::iterator i;
    // All attrpaths have at least one attr
    assert(!attrPath.empty());
    // Checking attrPath validity.
    // ===========================
    for (i = attrPath.begin(); i + 1 < attrPath.end(); i++) {
        if (i->symbol) {
            ExprAttrs::AttrDefs::iterator j = attrs->attrs.find(i->symbol);
            if (j != attrs->attrs.end()) {
                if (j->second.kind != ExprAttrs::AttrDef::Kind::Inherited) {
                    ExprAttrs * attrs2 = dynamic_cast<ExprAttrs *>(j->second.e.get());
                    if (!attrs2) dupAttr(attrPath, pos, j->second.pos);
                    attrs = attrs2;
                } else
                    dupAttr(attrPath, pos, j->second.pos);
            } else {
                auto next = attrs->attrs.emplace(std::piecewise_construct,
                    std::tuple(i->symbol),
                    std::tuple(std::make_unique<ExprAttrs>(), pos));
                attrs = static_cast<ExprAttrs *>(next.first->second.e.get());
            }
        } else {
            auto & next = attrs->dynamicAttrs.emplace_back(std::move(i->expr), std::make_unique<ExprAttrs>(), pos);
            attrs = static_cast<ExprAttrs *>(next.valueExpr.get());
        }
    }
    // Expr insertion.
    // ==========================
    if (i->symbol) {
        ExprAttrs::AttrDefs::iterator j = attrs->attrs.find(i->symbol);
        if (j != attrs->attrs.end()) {
            // This attr path is already defined. However, if both
            // e and the expr pointed by the attr path are two attribute sets,
            // we want to merge them.
            // Otherwise, throw an error.
            auto ae = dynamic_cast<ExprAttrs *>(e.get());
            auto jAttrs = dynamic_cast<ExprAttrs *>(j->second.e.get());
            if (jAttrs && ae) {
                if (ae->inheritFromExprs && !jAttrs->inheritFromExprs)
                    jAttrs->inheritFromExprs = std::make_unique<std::vector<std::unique_ptr<Expr>>>();
                for (auto & ad : ae->attrs) {
                    auto j2 = jAttrs->attrs.find(ad.first);
                    if (j2 != jAttrs->attrs.end()) // Attr already defined in iAttrs, error.
                        return dupAttr(ad.first, j2->second.pos, ad.second.pos);
                    if (ad.second.kind == ExprAttrs::AttrDef::Kind::InheritedFrom) {
                        auto & sel = dynamic_cast<ExprSelect &>(*ad.second.e);
                        auto & from = dynamic_cast<ExprInheritFrom &>(*sel.e);
                        from.displ += jAttrs->inheritFromExprs->size();
                    }
                    jAttrs->attrs.emplace(ad.first, std::move(ad.second));
                }
                std::ranges::move(ae->dynamicAttrs, std::back_inserter(jAttrs->dynamicAttrs));
                if (ae->inheritFromExprs)
                    std::ranges::move(*ae->inheritFromExprs, std::back_inserter(*jAttrs->inheritFromExprs));
            } else {
                dupAttr(attrPath, pos, j->second.pos);
            }
        } else {
            // This attr path is not defined. Let's create it.
            e->setName(i->symbol);
            attrs->attrs.emplace(std::piecewise_construct,
                std::tuple(i->symbol),
                std::tuple(std::move(e), pos));
        }
    } else {
        attrs->dynamicAttrs.emplace_back(std::move(i->expr), std::move(e), pos);
    }
}

inline std::unique_ptr<Formals> State::validateFormals(std::unique_ptr<Formals> formals, PosIdx pos, Symbol arg)
{
    std::sort(formals->formals.begin(), formals->formals.end(),
        [] (const auto & a, const auto & b) {
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
        throw ParseError({
            .msg = HintFmt("duplicate formal function argument '%1%'", symbols[duplicate->first]),
            .pos = positions[duplicate->second]
        });

    if (arg && formals->has(arg))
        throw ParseError({
            .msg = HintFmt("duplicate formal function argument '%1%'", symbols[arg]),
            .pos = positions[pos]
        });

    return formals;
}

inline std::unique_ptr<Expr> State::stripIndentation(const PosIdx pos,
    std::vector<std::pair<PosIdx, std::variant<std::unique_ptr<Expr>, StringToken>>> && es)
{
    if (es.empty()) return std::make_unique<ExprString>("");

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
                if (curIndent < minIndent) minIndent = curIndent;
            }
            continue;
        }
        for (size_t j = 0; j < str->s.size(); ++j) {
            if (atStartOfLine) {
                if (str->s[j] == ' ')
                    curIndent++;
                else if (str->s[j] == '\n') {
                    /* Empty line, doesn't influence minimum
                       indentation. */
                    curIndent = 0;
                } else {
                    atStartOfLine = false;
                    if (curIndent < minIndent) minIndent = curIndent;
                }
            } else if (str->s[j] == '\n') {
                atStartOfLine = true;
                curIndent = 0;
            }
        }
    }

    /* Strip spaces from each line. */
    std::vector<std::pair<PosIdx, std::unique_ptr<Expr>>> es2;
    atStartOfLine = true;
    size_t curDropped = 0;
    size_t n = es.size();
    auto i = es.begin();
    const auto trimExpr = [&] (std::unique_ptr<Expr> & e) {
        atStartOfLine = false;
        curDropped = 0;
        es2.emplace_back(i->first, std::move(e));
    };
    const auto trimString = [&] (const StringToken & t) {
        std::string s2;
        for (size_t j = 0; j < t.s.size(); ++j) {
            if (atStartOfLine) {
                if (t.s[j] == ' ') {
                    if (curDropped++ >= minIndent)
                        s2 += t.s[j];
                }
                else if (t.s[j] == '\n') {
                    curDropped = 0;
                    s2 += t.s[j];
                } else {
                    atStartOfLine = false;
                    curDropped = 0;
                    s2 += t.s[j];
                }
            } else {
                s2 += t.s[j];
                if (t.s[j] == '\n') atStartOfLine = true;
            }
        }

        /* Remove the last line if it is empty and consists only of
           spaces. */
        if (n == 1) {
            std::string::size_type p = s2.find_last_of('\n');
            if (p != std::string::npos && s2.find_first_not_of(' ', p + 1) == std::string::npos)
                s2 = std::string(s2, 0, p + 1);
        }

        es2.emplace_back(i->first, new ExprString(std::move(s2)));
    };
    for (; i != es.end(); ++i, --n) {
        std::visit(overloaded { trimExpr, trimString }, i->second);
    }

    /* If this is a single string, then don't do a concatenation. */
    if (es2.size() == 1 && dynamic_cast<ExprString *>(es2[0].second.get())) {
        return std::move(es2[0].second);
    }
    return std::make_unique<ExprConcatStrings>(pos, true, std::move(es2));
}

}
