#pragma once
///@file

#include <limits>

#include "eval.hh"

namespace nix {

/**
 * @note Storing a C-style `char *` and `size_t` allows us to avoid
 * having to define the special members that using string_view here
 * would implicitly delete.
 */
struct StringToken
{
    const char * p;
    size_t l;
    bool hasIndentation;
    operator std::string_view() const { return {p, l}; }
};

// This type must be trivially copyable; see YYLTYPE_IS_TRIVIAL in parser.y.
struct ParserLocation
{
    int beginOffset;
    int endOffset;

    // backup to recover from yyless(0)
    int stashedBeginOffset, stashedEndOffset;

    void stash() {
        stashedBeginOffset = beginOffset;
        stashedEndOffset = endOffset;
    }

    void unstash() {
        beginOffset = stashedBeginOffset;
        endOffset = stashedEndOffset;
    }

    /** Latest doc comment position, or 0. */
    int doc_comment_first_column, doc_comment_last_column;
};

struct LexerState
{
    /**
     * Tracks the distance to the last doc comment, in terms of lexer tokens.
     *
     * The lexer sets this to 0 when reading a doc comment, and increments it
     * for every matched rule; see `lexer-helpers.cc`.
     * Whitespace and comment rules decrement the distance, so that they result
     * in a net 0 change in distance.
     */
    int docCommentDistance = std::numeric_limits<int>::max();

    /**
     * The location of the last doc comment.
     *
     * (stashing fields are not used)
     */
    ParserLocation lastDocCommentLoc;

    /**
     * @brief Maps some positions to a DocComment, where the comment is relevant to the location.
     */
    std::unordered_map<PosIdx, DocComment> & positionToDocComment;

    PosTable & positions;
    PosTable::Origin origin;

    PosIdx at(const ParserLocation & loc);
};

struct ParserState
{
    const LexerState & lexerState;
    SymbolTable & symbols;
    PosTable & positions;
    Expr * result;
    SourcePath basePath;
    PosTable::Origin origin;
    const ref<SourceAccessor> rootFS;
    const Expr::AstSymbols & s;
    const EvalSettings & settings;

    void dupAttr(const AttrPath & attrPath, const PosIdx pos, const PosIdx prevPos);
    void dupAttr(Symbol attr, const PosIdx pos, const PosIdx prevPos);
    void addAttr(ExprAttrs * attrs, AttrPath && attrPath, const ParserLocation & loc, Expr * e, const ParserLocation & exprLoc);
    Formals * validateFormals(Formals * formals, PosIdx pos = noPos, Symbol arg = {});
    Expr * stripIndentation(const PosIdx pos,
        std::vector<std::pair<PosIdx, std::variant<Expr *, StringToken>>> && es);
    PosIdx at(const ParserLocation & loc);
};

inline void ParserState::dupAttr(const AttrPath & attrPath, const PosIdx pos, const PosIdx prevPos)
{
    throw ParseError({
         .msg = HintFmt("attribute '%1%' already defined at %2%",
             showAttrPath(symbols, attrPath), positions[prevPos]),
         .pos = positions[pos]
    });
}

inline void ParserState::dupAttr(Symbol attr, const PosIdx pos, const PosIdx prevPos)
{
    throw ParseError({
        .msg = HintFmt("attribute '%1%' already defined at %2%", symbols[attr], positions[prevPos]),
        .pos = positions[pos]
    });
}

inline void ParserState::addAttr(ExprAttrs * attrs, AttrPath && attrPath, const ParserLocation & loc, Expr * e, const ParserLocation & exprLoc)
{
    AttrPath::iterator i;
    // All attrpaths have at least one attr
    assert(!attrPath.empty());
    auto pos = at(loc);
    // Checking attrPath validity.
    // ===========================
    for (i = attrPath.begin(); i + 1 < attrPath.end(); i++) {
        if (i->symbol) {
            ExprAttrs::AttrDefs::iterator j = attrs->attrs.find(i->symbol);
            if (j != attrs->attrs.end()) {
                if (j->second.kind != ExprAttrs::AttrDef::Kind::Inherited) {
                    ExprAttrs * attrs2 = dynamic_cast<ExprAttrs *>(j->second.e);
                    if (!attrs2) dupAttr(attrPath, pos, j->second.pos);
                    attrs = attrs2;
                } else
                    dupAttr(attrPath, pos, j->second.pos);
            } else {
                ExprAttrs * nested = new ExprAttrs;
                attrs->attrs[i->symbol] = ExprAttrs::AttrDef(nested, pos);
                attrs = nested;
            }
        } else {
            ExprAttrs *nested = new ExprAttrs;
            attrs->dynamicAttrs.push_back(ExprAttrs::DynamicAttrDef(i->expr, nested, pos));
            attrs = nested;
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
            auto ae = dynamic_cast<ExprAttrs *>(e);
            auto jAttrs = dynamic_cast<ExprAttrs *>(j->second.e);
            if (jAttrs && ae) {
                if (ae->inheritFromExprs && !jAttrs->inheritFromExprs)
                    jAttrs->inheritFromExprs = std::make_unique<std::vector<Expr *>>();
                for (auto & ad : ae->attrs) {
                    auto j2 = jAttrs->attrs.find(ad.first);
                    if (j2 != jAttrs->attrs.end()) // Attr already defined in iAttrs, error.
                        dupAttr(ad.first, j2->second.pos, ad.second.pos);
                    jAttrs->attrs.emplace(ad.first, ad.second);
                    if (ad.second.kind == ExprAttrs::AttrDef::Kind::InheritedFrom) {
                        auto & sel = dynamic_cast<ExprSelect &>(*ad.second.e);
                        auto & from = dynamic_cast<ExprInheritFrom &>(*sel.e);
                        from.displ += jAttrs->inheritFromExprs->size();
                    }
                }
                jAttrs->dynamicAttrs.insert(jAttrs->dynamicAttrs.end(), ae->dynamicAttrs.begin(), ae->dynamicAttrs.end());
                if (ae->inheritFromExprs) {
                    jAttrs->inheritFromExprs->insert(jAttrs->inheritFromExprs->end(),
                        ae->inheritFromExprs->begin(), ae->inheritFromExprs->end());
                }
            } else {
                dupAttr(attrPath, pos, j->second.pos);
            }
        } else {
            // This attr path is not defined. Let's create it.
            attrs->attrs.emplace(i->symbol, ExprAttrs::AttrDef(e, pos));
            e->setName(i->symbol);
        }
    } else {
        attrs->dynamicAttrs.push_back(ExprAttrs::DynamicAttrDef(i->expr, e, pos));
    }

    auto it = lexerState.positionToDocComment.find(pos);
    if (it != lexerState.positionToDocComment.end()) {
        e->setDocComment(it->second);
        lexerState.positionToDocComment.emplace(at(exprLoc), it->second);
    }
}

inline Formals * ParserState::validateFormals(Formals * formals, PosIdx pos, Symbol arg)
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

inline Expr * ParserState::stripIndentation(const PosIdx pos,
    std::vector<std::pair<PosIdx, std::variant<Expr *, StringToken>>> && es)
{
    if (es.empty()) return new ExprString("");

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
                    if (curIndent < minIndent) minIndent = curIndent;
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
    const auto trimExpr = [&] (Expr * e) {
        atStartOfLine = false;
        curDropped = 0;
        es2->emplace_back(i->first, e);
    };
    const auto trimString = [&] (const StringToken & t) {
        std::string s2;
        for (size_t j = 0; j < t.l; ++j) {
            if (atStartOfLine) {
                if (t.p[j] == ' ') {
                    if (curDropped++ >= minIndent)
                        s2 += t.p[j];
                }
                else if (t.p[j] == '\n') {
                    curDropped = 0;
                    s2 += t.p[j];
                } else {
                    atStartOfLine = false;
                    curDropped = 0;
                    s2 += t.p[j];
                }
            } else {
                s2 += t.p[j];
                if (t.p[j] == '\n') atStartOfLine = true;
            }
        }

        /* Remove the last line if it is empty and consists only of
           spaces. */
        if (n == 1) {
            std::string::size_type p = s2.find_last_of('\n');
            if (p != std::string::npos && s2.find_first_not_of(' ', p + 1) == std::string::npos)
                s2 = std::string(s2, 0, p + 1);
        }

        // Ignore empty strings for a minor optimisation and AST simplification
        if (s2 != "") {
            es2->emplace_back(i->first, new ExprString(std::move(s2)));
        }
    };
    for (; i != es.end(); ++i, --n) {
        std::visit(overloaded { trimExpr, trimString }, i->second);
    }

    // If there is nothing at all, return the empty string directly.
    // This also ensures that equivalent empty strings result in the same ast, which is helpful when testing formatters.
    if (es2->size() == 0) {
        auto *const result = new ExprString("");
        delete es2;
        return result;
    }

    /* If this is a single string, then don't do a concatenation. */
    if (es2->size() == 1 && dynamic_cast<ExprString *>((*es2)[0].second)) {
        auto *const result = (*es2)[0].second;
        delete es2;
        return result;
    }
    return new ExprConcatStrings(pos, true, es2);
}

inline PosIdx LexerState::at(const ParserLocation & loc)
{
    return positions.add(origin, loc.beginOffset);
}

inline PosIdx ParserState::at(const ParserLocation & loc)
{
    return positions.add(origin, loc.beginOffset);
}

}
