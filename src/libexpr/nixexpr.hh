#ifndef __NIXEXPR_H
#define __NIXEXPR_H

#include "symbol-table.hh"

#include <map>


namespace nix {


MakeError(EvalError, Error)
MakeError(ParseError, Error)
MakeError(AssertionError, EvalError)
MakeError(ThrownError, AssertionError)
MakeError(Abort, EvalError)
MakeError(TypeError, EvalError)
MakeError(ImportError, EvalError) // error building an imported derivation


/* Position objects. */

struct Pos
{
    string file;
    unsigned int line, column;
    Pos() : line(0), column(0) { };
    Pos(const string & file, unsigned int line, unsigned int column)
        : file(file), line(line), column(column) { };
};

extern Pos noPos;

std::ostream & operator << (std::ostream & str, const Pos & pos);


struct Env;
struct Value;
struct EvalState;
struct StaticEnv;


/* Abstract syntax of Nix expressions. */

struct Expr
{
    virtual void show(std::ostream & str);
    virtual void bindVars(const StaticEnv & env);
    virtual void eval(EvalState & state, Env & env, Value & v);
};

std::ostream & operator << (std::ostream & str, Expr & e);

#define COMMON_METHODS \
    void show(std::ostream & str); \
    void eval(EvalState & state, Env & env, Value & v); \
    void bindVars(const StaticEnv & env);

struct ExprInt : Expr
{
    int n;
    ExprInt(int n) : n(n) { };
    COMMON_METHODS
};

struct ExprString : Expr
{
    string s;
    ExprString(const string & s) : s(s) { };
    COMMON_METHODS
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
    ExprPath(const string & s) : s(s) { };
    COMMON_METHODS
};

struct VarRef
{
    Symbol name;

    /* Whether the variable comes from an environment (e.g. a rec, let
       or function argument) or from a "with". */
    bool fromWith;
    
    /* In the former case, the value is obtained by going `level'
       levels up from the current environment and getting the
       `displ'th value in that environment.  In the latter case, the
       value is obtained by getting the attribute named `name' from
       the attribute set stored in the environment that is `level'
       levels up from the current one.*/
    unsigned int level;
    unsigned int displ;

    VarRef(const Symbol & name) : name(name) { };
    void bind(const StaticEnv & env);
};

struct ExprVar : Expr
{
    VarRef info;
    ExprVar(const Symbol & name) : info(name) { };
    COMMON_METHODS
};

struct ExprSelect : Expr
{
    Expr * e;
    Symbol name;
    ExprSelect(Expr * e, const Symbol & name) : e(e), name(name) { };
    COMMON_METHODS
};

struct ExprOpHasAttr : Expr
{
    Expr * e;
    Symbol name;
    ExprOpHasAttr(Expr * e, const Symbol & name) : e(e), name(name) { };
    COMMON_METHODS
};

struct ExprAttrs : Expr
{
    bool recursive;
    typedef std::pair<Expr *, Pos> Attr;
    typedef std::pair<VarRef, Pos> Inherited;
    typedef std::map<Symbol, Attr> Attrs;
    Attrs attrs;
    list<Inherited> inherited;
    std::map<Symbol, Pos> attrNames; // used during parsing
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
    Symbol arg;
    bool matchAttrs;
    Formals * formals;
    Expr * body;
    ExprLambda(const Pos & pos, const Symbol & arg, bool matchAttrs, Formals * formals, Expr * body)
        : pos(pos), arg(arg), matchAttrs(matchAttrs), formals(formals), body(body)
    {
        if (!arg.empty() && formals && formals->argNames.find(arg) != formals->argNames.end())
            throw ParseError(format("duplicate formal function argument `%1%' at %2%")
                % arg % pos);
    };
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
    Expr * cond, * then, * else_;
    ExprIf(Expr * cond, Expr * then, Expr * else_) : cond(cond), then(then), else_(else_) { };
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
    struct Expr##name : Expr \
    { \
        Expr * e1, * e2; \
        Expr##name(Expr * e1, Expr * e2) : e1(e1), e2(e2) { }; \
        void show(std::ostream & str) \
        { \
            str << *e1 << " " s " " << *e2; \
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
    vector<Expr *> * es;
    ExprConcatStrings(vector<Expr *> * es) : es(es) { };
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


#endif /* !__NIXEXPR_H */
