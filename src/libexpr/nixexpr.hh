#pragma once

#include "value.hh"
#include "symbol-table.hh"
#include "error.hh"


namespace nix {


MakeError(EvalError, Error);
MakeError(ParseError, Error);
MakeError(AssertionError, EvalError);
MakeError(ThrownError, AssertionError);
MakeError(Abort, EvalError);
MakeError(TypeError, EvalError);
MakeError(UndefinedVarError, Error);
MakeError(MissingArgumentError, EvalError);
MakeError(RestrictedPathError, Error);


/* Position objects. */

struct Pos
{
    Symbol file;
    uint32_t line;
    FileOrigin origin:2;
    uint32_t column:30;
    Pos() : line(0), origin(foString), column(0) { };
    Pos(FileOrigin origin, const Symbol & file, uint32_t line, uint32_t column)
        : file(file), line(line), origin(origin), column(column) { };
    operator bool() const
    {
        return line != 0;
    }

    bool operator < (const Pos & p2) const
    {
        if (!line) return p2.line;
        if (!p2.line) return false;
        int d = ((const std::string &) file).compare((const std::string &) p2.file);
        if (d < 0) return true;
        if (d > 0) return false;
        if (line < p2.line) return true;
        if (line > p2.line) return false;
        return column < p2.column;
    }
};

extern Pos noPos;

std::ostream & operator << (std::ostream & str, const Pos & pos);


struct Env;
struct Value;
class EvalState;
struct StaticEnv;


/* An attribute path is a sequence of attribute names. */
struct AttrName
{
    Symbol symbol;
    Expr * expr;
    AttrName(const Symbol & s) : symbol(s) {};
    AttrName(Expr * e) : expr(e) {};
};

typedef std::vector<AttrName> AttrPath;

std::string showAttrPath(const AttrPath & attrPath);


/* Abstract syntax of Nix expressions. */

struct Expr
{
    virtual ~Expr() { };
    virtual void show(std::ostream & str) const;
    virtual void bindVars(const StaticEnv & env);
    virtual void eval(EvalState & state, Env & env, Value & v);
    virtual Value * maybeThunk(EvalState & state, Env & env);
    virtual void setName(Symbol & name);
};

std::ostream & operator << (std::ostream & str, const Expr & e);

#define COMMON_METHODS \
    void show(std::ostream & str) const; \
    void eval(EvalState & state, Env & env, Value & v); \
    void bindVars(const StaticEnv & env);

struct ExprInt : Expr
{
    NixInt n;
    Value v;
    ExprInt(NixInt n) : n(n) { v.mkInt(n); };
    COMMON_METHODS
    Value * maybeThunk(EvalState & state, Env & env);
};

struct ExprFloat : Expr
{
    NixFloat nf;
    Value v;
    ExprFloat(NixFloat nf) : nf(nf) { v.mkFloat(nf); };
    COMMON_METHODS
    Value * maybeThunk(EvalState & state, Env & env);
};

struct ExprString : Expr
{
    std::string s;
    Value v;
    ExprString(std::string s) : s(std::move(s)) { v.mkString(this->s.data()); };
    COMMON_METHODS
    Value * maybeThunk(EvalState & state, Env & env);
};

struct ExprPath : Expr
{
    std::string s;
    Value v;
    ExprPath(std::string s) : s(std::move(s)) { v.mkPath(this->s.c_str()); };
    COMMON_METHODS
    Value * maybeThunk(EvalState & state, Env & env);
};

typedef uint32_t Level;
typedef uint32_t Displacement;

struct ExprVar : Expr
{
    Pos pos;
    Symbol name;

    /* Whether the variable comes from an environment (e.g. a rec, let
       or function argument) or from a "with". */
    bool fromWith;

    /* In the former case, the value is obtained by going `level'
       levels up from the current environment and getting the
       `displ'th value in that environment.  In the latter case, the
       value is obtained by getting the attribute named `name' from
       the set stored in the environment that is `level' levels up
       from the current one.*/
    Level level;
    Displacement displ;

    ExprVar(const Symbol & name) : name(name) { };
    ExprVar(const Pos & pos, const Symbol & name) : pos(pos), name(name) { };
    COMMON_METHODS
    Value * maybeThunk(EvalState & state, Env & env);
};

struct ExprSelect : Expr
{
    Pos pos;
    Expr * e, * def;
    AttrPath attrPath;
    ExprSelect(const Pos & pos, Expr * e, const AttrPath & attrPath, Expr * def) : pos(pos), e(e), def(def), attrPath(attrPath) { };
    ExprSelect(const Pos & pos, Expr * e, const Symbol & name) : pos(pos), e(e), def(0) { attrPath.push_back(AttrName(name)); };
    COMMON_METHODS
};

struct ExprOpHasAttr : Expr
{
    Expr * e;
    AttrPath attrPath;
    ExprOpHasAttr(Expr * e, const AttrPath & attrPath) : e(e), attrPath(attrPath) { };
    COMMON_METHODS
};

struct ExprAttrs : Expr
{
    bool recursive;
    Pos pos;
    struct AttrDef {
        bool inherited;
        Expr * e;
        Pos pos;
        Displacement displ; // displacement
        AttrDef(Expr * e, const Pos & pos, bool inherited=false)
            : inherited(inherited), e(e), pos(pos) { };
        AttrDef() { };
    };
    typedef std::map<Symbol, AttrDef> AttrDefs;
    AttrDefs attrs;
    struct DynamicAttrDef {
        Expr * nameExpr, * valueExpr;
        Pos pos;
        DynamicAttrDef(Expr * nameExpr, Expr * valueExpr, const Pos & pos)
            : nameExpr(nameExpr), valueExpr(valueExpr), pos(pos) { };
    };
    typedef std::vector<DynamicAttrDef> DynamicAttrDefs;
    DynamicAttrDefs dynamicAttrs;
    ExprAttrs(const Pos &pos) : recursive(false), pos(pos) { };
    ExprAttrs() : recursive(false), pos(noPos) { };
    COMMON_METHODS
};

struct ExprList : Expr
{
    std::vector<Expr *> elems;
    ExprList() { };
    COMMON_METHODS
};

struct Formal
{
    Pos pos;
    Symbol name;
    Expr * def;
    Formal(const Pos & pos, const Symbol & name, Expr * def) : pos(pos), name(name), def(def) { };
};

struct Formals
{
    typedef std::vector<Formal> Formals_;
    Formals_ formals;
    bool ellipsis;

    bool has(Symbol arg) const {
        auto it = std::lower_bound(formals.begin(), formals.end(), arg,
            [] (const Formal & f, const Symbol & sym) { return f.name < sym; });
        return it != formals.end() && it->name == arg;
    }

    std::vector<Formal> lexicographicOrder() const
    {
        std::vector<Formal> result(formals.begin(), formals.end());
        std::sort(result.begin(), result.end(),
            [] (const Formal & a, const Formal & b) {
                return std::string_view(a.name) < std::string_view(b.name);
            });
        return result;
    }
};

struct ExprLambda : Expr
{
    Pos pos;
    Symbol name;
    Symbol arg;
    Formals * formals;
    Expr * body;
    ExprLambda(const Pos & pos, const Symbol & arg, Formals * formals, Expr * body)
        : pos(pos), arg(arg), formals(formals), body(body)
    {
    };
    void setName(Symbol & name);
    std::string showNamePos() const;
    inline bool hasFormals() const { return formals != nullptr; }
    COMMON_METHODS
};

struct ExprCall : Expr
{
    Expr * fun;
    std::vector<Expr *> args;
    Pos pos;
    ExprCall(const Pos & pos, Expr * fun, std::vector<Expr *> && args)
        : fun(fun), args(args), pos(pos)
    { }
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
    Pos pos;
    Expr * attrs, * body;
    size_t prevWith;
    ExprWith(const Pos & pos, Expr * attrs, Expr * body) : pos(pos), attrs(attrs), body(body) { };
    COMMON_METHODS
};

struct ExprIf : Expr
{
    Pos pos;
    Expr * cond, * then, * else_;
    ExprIf(const Pos & pos, Expr * cond, Expr * then, Expr * else_) : pos(pos), cond(cond), then(then), else_(else_) { };
    COMMON_METHODS
};

struct ExprAssert : Expr
{
    Pos pos;
    Expr * cond, * body;
    ExprAssert(const Pos & pos, Expr * cond, Expr * body) : pos(pos), cond(cond), body(body) { };
    COMMON_METHODS
};

struct ExprOpNot : Expr
{
    Expr * e;
    ExprOpNot(Expr * e) : e(e) { };
    COMMON_METHODS
};

#define MakeBinOp(name, s) \
    struct name : Expr \
    { \
        Pos pos; \
        Expr * e1, * e2; \
        name(Expr * e1, Expr * e2) : e1(e1), e2(e2) { }; \
        name(const Pos & pos, Expr * e1, Expr * e2) : pos(pos), e1(e1), e2(e2) { }; \
        void show(std::ostream & str) const \
        { \
            str << "(" << *e1 << " " s " " << *e2 << ")";   \
        } \
        void bindVars(const StaticEnv & env) \
        { \
            e1->bindVars(env); e2->bindVars(env); \
        } \
        void eval(EvalState & state, Env & env, Value & v); \
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
    Pos pos;
    bool forceString;
    std::vector<std::pair<Pos, Expr *> > * es;
    ExprConcatStrings(const Pos & pos, bool forceString, std::vector<std::pair<Pos, Expr *> > * es)
        : pos(pos), forceString(forceString), es(es) { };
    COMMON_METHODS
};

struct ExprPos : Expr
{
    Pos pos;
    ExprPos(const Pos & pos) : pos(pos) { };
    COMMON_METHODS
};


/* Static environments are used to map variable names onto (level,
   displacement) pairs used to obtain the value of the variable at
   runtime. */
struct StaticEnv
{
    bool isWith;
    const StaticEnv * up;

    // Note: these must be in sorted order.
    typedef std::vector<std::pair<Symbol, Displacement>> Vars;
    Vars vars;

    StaticEnv(bool isWith, const StaticEnv * up, size_t expectedSize = 0) : isWith(isWith), up(up) {
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

    Vars::const_iterator find(const Symbol & name) const
    {
        Vars::value_type key(name, 0);
        auto i = std::lower_bound(vars.begin(), vars.end(), key);
        if (i != vars.end() && i->first == name) return i;
        return vars.end();
    }
};


}
