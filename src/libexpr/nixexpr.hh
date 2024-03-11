#pragma once
///@file

#include <map>
#include <vector>

#include "value.hh"
#include "symbol-table.hh"
#include "error.hh"
#include "position.hh"
#include "eval-error.hh"
#include "pos-idx.hh"
#include "pos-table.hh"

namespace nix {


struct Env;
struct Value;
class EvalState;
struct ExprWith;
struct StaticEnv;


/**
 * An attribute path is a sequence of attribute names.
 */
struct AttrName
{
    Symbol symbol;
    Expr * expr;
    AttrName(Symbol s) : symbol(s) {};
    AttrName(Expr * e) : expr(e) {};
};

typedef std::vector<AttrName> AttrPath;

std::string showAttrPath(const SymbolTable & symbols, const AttrPath & attrPath);


/* Abstract syntax of Nix expressions. */

struct Expr
{
    struct AstSymbols {
        Symbol sub, lessThan, mul, div, or_, findFile, nixPath, body;
    };


    static unsigned long nrExprs;
    Expr() {
        nrExprs++;
    }
    virtual ~Expr() { };
    virtual void show(const SymbolTable & symbols, std::ostream & str) const;
    virtual void bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env);
    virtual void eval(EvalState & state, Env & env, Value & v);
    virtual Value * maybeThunk(EvalState & state, Env & env);
    virtual void setName(Symbol name);
    virtual PosIdx getPos() const { return noPos; }
};

#define COMMON_METHODS \
    void show(const SymbolTable & symbols, std::ostream & str) const override; \
    void eval(EvalState & state, Env & env, Value & v) override; \
    void bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env) override;

struct ExprInt : Expr
{
    Value v;
    ExprInt(NixInt n) { v.mkInt(n); };
    Value * maybeThunk(EvalState & state, Env & env) override;
    COMMON_METHODS
};

struct ExprFloat : Expr
{
    Value v;
    ExprFloat(NixFloat nf) { v.mkFloat(nf); };
    Value * maybeThunk(EvalState & state, Env & env) override;
    COMMON_METHODS
};

struct ExprString : Expr
{
    std::string s;
    Value v;
    ExprString(std::string &&s) : s(std::move(s)) { v.mkString(this->s.data()); };
    Value * maybeThunk(EvalState & state, Env & env) override;
    COMMON_METHODS
};

struct ExprPath : Expr
{
    ref<InputAccessor> accessor;
    std::string s;
    Value v;
    ExprPath(ref<InputAccessor> accessor, std::string s) : accessor(accessor), s(std::move(s))
    {
        v.mkPath(&*accessor, this->s.c_str());
    }
    Value * maybeThunk(EvalState & state, Env & env) override;
    COMMON_METHODS
};

typedef uint32_t Level;
typedef uint32_t Displacement;

struct ExprVar : Expr
{
    PosIdx pos;
    Symbol name;

    /* Whether the variable comes from an environment (e.g. a rec, let
       or function argument) or from a "with".

       `nullptr`: Not from a `with`.
       Valid pointer: the nearest, innermost `with` expression to query first. */
    ExprWith * fromWith;

    /* In the former case, the value is obtained by going `level`
       levels up from the current environment and getting the
       `displ`th value in that environment.  In the latter case, the
       value is obtained by getting the attribute named `name` from
       the set stored in the environment that is `level` levels up
       from the current one.*/
    Level level;
    Displacement displ;

    ExprVar(Symbol name) : name(name) { };
    ExprVar(const PosIdx & pos, Symbol name) : pos(pos), name(name) { };
    Value * maybeThunk(EvalState & state, Env & env) override;
    PosIdx getPos() const override { return pos; }
    COMMON_METHODS
};

/**
 * A pseudo-expression for the purpose of evaluating the `from` expression in `inherit (from)` syntax.
 * Unlike normal variable references, the displacement is set during parsing, and always refers to
 * `ExprAttrs::inheritFromExprs` (by itself or in `ExprLet`), whose values are put into their own `Env`.
 */
struct ExprInheritFrom : ExprVar
{
    ExprInheritFrom(PosIdx pos, Displacement displ): ExprVar(pos, {})
    {
        this->level = 0;
        this->displ = displ;
        this->fromWith = nullptr;
    }

    void bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env);
};

struct ExprSelect : Expr
{
    PosIdx pos;
    Expr * e, * def;
    AttrPath attrPath;
    ExprSelect(const PosIdx & pos, Expr * e, AttrPath attrPath, Expr * def) : pos(pos), e(e), def(def), attrPath(std::move(attrPath)) { };
    ExprSelect(const PosIdx & pos, Expr * e, Symbol name) : pos(pos), e(e), def(0) { attrPath.push_back(AttrName(name)); };
    PosIdx getPos() const override { return pos; }
    COMMON_METHODS
};

struct ExprOpHasAttr : Expr
{
    Expr * e;
    AttrPath attrPath;
    ExprOpHasAttr(Expr * e, AttrPath attrPath) : e(e), attrPath(std::move(attrPath)) { };
    PosIdx getPos() const override { return e->getPos(); }
    COMMON_METHODS
};

struct ExprAttrs : Expr
{
    bool recursive;
    PosIdx pos;
    struct AttrDef {
        enum class Kind {
            /** `attr = expr;` */
            Plain,
            /** `inherit attr1 attrn;` */
            Inherited,
            /** `inherit (expr) attr1 attrn;` */
            InheritedFrom,
        };

        Kind kind;
        Expr * e;
        PosIdx pos;
        Displacement displ; // displacement
        AttrDef(Expr * e, const PosIdx & pos, Kind kind = Kind::Plain)
            : kind(kind), e(e), pos(pos) { };
        AttrDef() { };

        template<typename T>
        const T & chooseByKind(const T & plain, const T & inherited, const T & inheritedFrom) const
        {
            switch (kind) {
            case Kind::Plain:
                return plain;
            case Kind::Inherited:
                return inherited;
            default:
            case Kind::InheritedFrom:
                return inheritedFrom;
            }
        }
    };
    typedef std::map<Symbol, AttrDef> AttrDefs;
    AttrDefs attrs;
    std::unique_ptr<std::vector<Expr *>> inheritFromExprs;
    struct DynamicAttrDef {
        Expr * nameExpr, * valueExpr;
        PosIdx pos;
        DynamicAttrDef(Expr * nameExpr, Expr * valueExpr, const PosIdx & pos)
            : nameExpr(nameExpr), valueExpr(valueExpr), pos(pos) { };
    };
    typedef std::vector<DynamicAttrDef> DynamicAttrDefs;
    DynamicAttrDefs dynamicAttrs;
    ExprAttrs(const PosIdx &pos) : recursive(false), pos(pos) { };
    ExprAttrs() : recursive(false) { };
    PosIdx getPos() const override { return pos; }
    COMMON_METHODS

    std::shared_ptr<const StaticEnv> bindInheritSources(
        EvalState & es, const std::shared_ptr<const StaticEnv> & env);
    Env * buildInheritFromEnv(EvalState & state, Env & up);
    void showBindings(const SymbolTable & symbols, std::ostream & str) const;
};

struct ExprList : Expr
{
    std::vector<Expr *> elems;
    ExprList() { };
    COMMON_METHODS
    Value * maybeThunk(EvalState & state, Env & env) override;

    PosIdx getPos() const override
    {
        return elems.empty() ? noPos : elems.front()->getPos();
    }
};

struct Formal
{
    PosIdx pos;
    Symbol name;
    Expr * def;
};

struct Formals
{
    typedef std::vector<Formal> Formals_;
    Formals_ formals;
    bool ellipsis;

    bool has(Symbol arg) const
    {
        auto it = std::lower_bound(formals.begin(), formals.end(), arg,
            [] (const Formal & f, const Symbol & sym) { return f.name < sym; });
        return it != formals.end() && it->name == arg;
    }

    std::vector<Formal> lexicographicOrder(const SymbolTable & symbols) const
    {
        std::vector<Formal> result(formals.begin(), formals.end());
        std::sort(result.begin(), result.end(),
            [&] (const Formal & a, const Formal & b) {
                std::string_view sa = symbols[a.name], sb = symbols[b.name];
                return sa < sb;
            });
        return result;
    }
};

struct ExprLambda : Expr
{
    PosIdx pos;
    Symbol name;
    Symbol arg;
    Formals * formals;
    Expr * body;
    ExprLambda(PosIdx pos, Symbol arg, Formals * formals, Expr * body)
        : pos(pos), arg(arg), formals(formals), body(body)
    {
    };
    ExprLambda(PosIdx pos, Formals * formals, Expr * body)
        : pos(pos), formals(formals), body(body)
    {
    }
    void setName(Symbol name) override;
    std::string showNamePos(const EvalState & state) const;
    inline bool hasFormals() const { return formals != nullptr; }
    PosIdx getPos() const override { return pos; }
    COMMON_METHODS
};

struct ExprCall : Expr
{
    Expr * fun;
    std::vector<Expr *> args;
    PosIdx pos;
    ExprCall(const PosIdx & pos, Expr * fun, std::vector<Expr *> && args)
        : fun(fun), args(args), pos(pos)
    { }
    PosIdx getPos() const override { return pos; }
    COMMON_METHODS
};

struct ExprLet : Expr
{
    ExprAttrs * attrs;
    Expr * body;
    ExprLet(ExprAttrs * attrs, Expr * body) : attrs(attrs), body(body) { };
    COMMON_METHODS
};

struct ExprWith : Expr
{
    PosIdx pos;
    Expr * attrs, * body;
    size_t prevWith;
    ExprWith * parentWith;
    ExprWith(const PosIdx & pos, Expr * attrs, Expr * body) : pos(pos), attrs(attrs), body(body) { };
    PosIdx getPos() const override { return pos; }
    COMMON_METHODS
};

struct ExprIf : Expr
{
    PosIdx pos;
    Expr * cond, * then, * else_;
    ExprIf(const PosIdx & pos, Expr * cond, Expr * then, Expr * else_) : pos(pos), cond(cond), then(then), else_(else_) { };
    PosIdx getPos() const override { return pos; }
    COMMON_METHODS
};

struct ExprAssert : Expr
{
    PosIdx pos;
    Expr * cond, * body;
    ExprAssert(const PosIdx & pos, Expr * cond, Expr * body) : pos(pos), cond(cond), body(body) { };
    PosIdx getPos() const override { return pos; }
    COMMON_METHODS
};

struct ExprOpNot : Expr
{
    Expr * e;
    ExprOpNot(Expr * e) : e(e) { };
    PosIdx getPos() const override { return e->getPos(); }
    COMMON_METHODS
};

#define MakeBinOp(name, s) \
    struct name : Expr \
    { \
        PosIdx pos; \
        Expr * e1, * e2; \
        name(Expr * e1, Expr * e2) : e1(e1), e2(e2) { }; \
        name(const PosIdx & pos, Expr * e1, Expr * e2) : pos(pos), e1(e1), e2(e2) { }; \
        void show(const SymbolTable & symbols, std::ostream & str) const override \
        { \
            str << "("; e1->show(symbols, str); str << " " s " "; e2->show(symbols, str); str << ")"; \
        } \
        void bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env) override \
        { \
            e1->bindVars(es, env); e2->bindVars(es, env);    \
        } \
        void eval(EvalState & state, Env & env, Value & v) override; \
        PosIdx getPos() const override { return pos; } \
    };

MakeBinOp(ExprOpEq, "==")
MakeBinOp(ExprOpNEq, "!=")
MakeBinOp(ExprOpAnd, "&&")
MakeBinOp(ExprOpOr, "||")
MakeBinOp(ExprOpImpl, "->")
MakeBinOp(ExprOpUpdate, "//")
MakeBinOp(ExprOpConcatLists, "++")

struct ExprConcatStrings : Expr
{
    PosIdx pos;
    bool forceString;
    std::vector<std::pair<PosIdx, Expr *>> * es;
    ExprConcatStrings(const PosIdx & pos, bool forceString, std::vector<std::pair<PosIdx, Expr *>> * es)
        : pos(pos), forceString(forceString), es(es) { };
    PosIdx getPos() const override { return pos; }
    COMMON_METHODS
};

struct ExprPos : Expr
{
    PosIdx pos;
    ExprPos(const PosIdx & pos) : pos(pos) { };
    PosIdx getPos() const override { return pos; }
    COMMON_METHODS
};

/* only used to mark thunks as black holes. */
struct ExprBlackHole : Expr
{
    void show(const SymbolTable & symbols, std::ostream & str) const override {}
    void eval(EvalState & state, Env & env, Value & v) override;
    void bindVars(EvalState & es, const std::shared_ptr<const StaticEnv> & env) override {}
};

extern ExprBlackHole eBlackHole;


/* Static environments are used to map variable names onto (level,
   displacement) pairs used to obtain the value of the variable at
   runtime. */
struct StaticEnv
{
    ExprWith * isWith;
    const StaticEnv * up;

    // Note: these must be in sorted order.
    typedef std::vector<std::pair<Symbol, Displacement>> Vars;
    Vars vars;

    StaticEnv(ExprWith * isWith, const StaticEnv * up, size_t expectedSize = 0) : isWith(isWith), up(up) {
        vars.reserve(expectedSize);
    };

    void sort()
    {
        std::stable_sort(vars.begin(), vars.end(),
            [](const Vars::value_type & a, const Vars::value_type & b) { return a.first < b.first; });
    }

    void deduplicate()
    {
        auto it = vars.begin(), jt = it, end = vars.end();
        while (jt != end) {
            *it = *jt++;
            while (jt != end && it->first == jt->first) *it = *jt++;
            it++;
        }
        vars.erase(it, end);
    }

    Vars::const_iterator find(Symbol name) const
    {
        Vars::value_type key(name, 0);
        auto i = std::lower_bound(vars.begin(), vars.end(), key);
        if (i != vars.end() && i->first == name) return i;
        return vars.end();
    }
};


}
