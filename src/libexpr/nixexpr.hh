#pragma once

#include "value.hh"
#include "symbol-table.hh"

#include <map>


namespace nix {


MakeError(EvalError, Error)
MakeError(ParseError, Error)
MakeError(AssertionError, EvalError)
MakeError(ThrownError, AssertionError)
MakeError(Abort, EvalError)
MakeError(TypeError, EvalError)
MakeError(UndefinedVarError, Error)
MakeError(RestrictedPathError, Error)


/* Position objects. */

struct Pos
{
    Symbol file;
    unsigned int firstline, firstcolumn, lastline, lastcolumn;
    Pos() : firstline(0), firstcolumn(0), lastline(-1),lastcolumn(-1) { };
    Pos(const Symbol & file, unsigned int firstline, unsigned int firstcolumn, unsigned int lastline, unsigned int lastcolumn)
        : file(file), firstline(firstline), firstcolumn(firstcolumn), lastline(lastline), lastcolumn(lastcolumn) { };
    operator bool() const
    {
        return firstline != 0;
    }
    bool operator < (const Pos & p2) const
    {
        if (!firstline) return p2.firstline;
        if (!p2.firstline) return false;
        int d = ((string) file).compare((string) p2.file);
        if (d < 0) return true;
        if (d > 0) return false;
        if (firstline < p2.firstline) return true;
        if (firstline > p2.firstline) return false;
        if (firstcolumn < p2.firstcolumn) return true;
        if (firstcolumn > p2.firstcolumn) return false;
        if (lastline < p2.lastline) return true;
        if (lastline > p2.lastline) return false;
        return lastcolumn < p2.lastcolumn;
    }
};

extern Pos noPos;

string showPos(const Pos& pos, const string& locType);
std::ostream & operator << (std::ostream & str, const Pos & pos);


struct Env;
struct Value;
class EvalState;
struct StaticEnv;


/* An attribute path is a sequence of attribute names. */
struct AttrName
{
    Pos pos;
    Symbol symbol;
    Expr * expr;
    AttrName(const Pos& pos, const Symbol & s) : pos(pos), symbol(s) {};
    AttrName(const Pos& pos, Expr * e) : pos(pos), expr(e) {};
};

typedef std::vector<AttrName> AttrPath;

string showAttrPath(const AttrPath & attrPath);


/* Abstract syntax of Nix expressions. */

struct Expr
{
    virtual ~Expr() { };
    virtual void show(std::ostream & str);
    virtual void bindVars(const StaticEnv & env);
    virtual void eval(EvalState & state, Env & env, Value & v);
    virtual Value * maybeThunk(EvalState & state, Env & env);
    virtual void setName(Symbol & name);
};

std::ostream & operator << (std::ostream & str, Expr & e);

#define COMMON_METHODS \
    void show(std::ostream & str); \
    void eval(EvalState & state, Env & env, Value & v); \
    void bindVars(const StaticEnv & env);

struct ExprInt : Expr
{
    NixInt n;
    Value v;
    ExprInt(NixInt n) : n(n) { mkInt(v, n); };
    COMMON_METHODS
    Value * maybeThunk(EvalState & state, Env & env);
};

struct ExprString : Expr
{
    Symbol s;
    Value v;
    ExprString(const Symbol & s) : s(s) { mkString(v, s); };
    COMMON_METHODS
    Value * maybeThunk(EvalState & state, Env & env);
};

/* Temporary class used during parsing of indented strings. */
struct ExprIndStr : Expr
{
    string s;
    ExprIndStr(const string & s) : s(s) { };
};

struct ExprPath : Expr
{
    string s;
    Value v;
    ExprPath(const string & s) : s(s) { mkPathNoCopy(v, this->s.c_str()); };
    COMMON_METHODS
    Value * maybeThunk(EvalState & state, Env & env);
};

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
    unsigned int level;
    unsigned int displ;

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
    ExprSelect(const Pos & pos, Expr * e, const Symbol & name) : pos(pos), e(e), def(0) { attrPath.push_back(AttrName(pos, name)); };
    COMMON_METHODS
};

struct ExprOpHasAttr : Expr
{
    Pos pos;
    Expr * e;
    AttrPath attrPath;
    ExprOpHasAttr(const Pos & pos, Expr * e, const AttrPath & attrPath) : pos(pos), e(e), attrPath(attrPath) { };
    COMMON_METHODS
};

struct ExprAttrs : Expr
{
    bool recursive;
    struct AttrDef {
        bool inherited;
        Expr * e;
        Pos pos;
        unsigned int displ; // displacement
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
    ExprAttrs() : recursive(false) { };
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
    Symbol name;
    Expr * def;
    Formal(const Symbol & name, Expr * def) : name(name), def(def) { };
};

struct Formals
{
    typedef std::list<Formal> Formals_;
    Formals_ formals;
    std::set<Symbol> argNames; // used during parsing
    bool ellipsis;
};

struct ExprLambda : Expr
{
    Pos pos;
    Symbol name;
    Symbol arg;
    bool matchAttrs;
    Formals * formals;
    Expr * body;
    ExprLambda(const Pos & pos, const Symbol & arg, bool matchAttrs, Formals * formals, Expr * body)
        : pos(pos), arg(arg), matchAttrs(matchAttrs), formals(formals), body(body)
    {
        if (!arg.empty() && formals && formals->argNames.find(arg) != formals->argNames.end())
            throw ParseError(format("duplicate formal function argument ‘%1%’%2%")
                % arg % pos);
    };
    void setName(Symbol & name);
    string showNamePos() const;
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
    unsigned int prevWith;
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
    Pos pos;
    Expr * e;
    ExprOpNot(const Pos & pos, Expr * e) : pos(pos), e(e) { };
    COMMON_METHODS
};

#define MakeBinOp(name, s) \
    struct Expr##name : Expr \
    { \
        Pos pos; \
        Expr * e1, * e2; \
        Expr##name(const Pos & pos, Expr * e1, Expr * e2) : pos(pos), e1(e1), e2(e2) { }; \
        void show(std::ostream & str) \
        { \
            str << "(" << *e1 << " " s " " << *e2 << ")";   \
        } \
        void bindVars(const StaticEnv & env) \
        { \
            e1->bindVars(env); e2->bindVars(env); \
        } \
        void eval(EvalState & state, Env & env, Value & v); \
    };

MakeBinOp(App, "")
MakeBinOp(OpEq, "==")
MakeBinOp(OpNEq, "!=")
MakeBinOp(OpAnd, "&&")
MakeBinOp(OpOr, "||")
MakeBinOp(OpImpl, "->")
MakeBinOp(OpUpdate, "//")
MakeBinOp(OpConcatLists, "++")

struct ExprConcatStrings : Expr
{
    Pos pos;
    bool forceString;
    vector<Expr *> * es;
    ExprConcatStrings(const Pos & pos, bool forceString, vector<Expr *> * es)
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
    typedef std::map<Symbol, unsigned int> Vars;
    Vars vars;
    StaticEnv(bool isWith, const StaticEnv * up) : isWith(isWith), up(up) { };
};


}
