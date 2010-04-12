#ifndef __NIXEXPR_H
#define __NIXEXPR_H

#include <map>

#include "types.hh"


namespace nix {


MakeError(EvalError, Error)
MakeError(ParseError, Error)
MakeError(AssertionError, EvalError)
MakeError(ThrownError, AssertionError)
MakeError(Abort, EvalError)
MakeError(TypeError, EvalError)


struct Pos
{
    string file;
    unsigned int line, column;
};


std::ostream & operator << (std::ostream & str, const Pos & pos);


/* Abstract syntax of Nix expressions. */

struct Env;
struct Value;
struct EvalState;

struct Expr
{
    virtual void show(std::ostream & str) = 0;
    virtual void eval(EvalState & state, Env & env, Value & v)
    {
        throw Error("not implemented");
    }
};

std::ostream & operator << (std::ostream & str, Expr & e);

#define COMMON_METHODS \
    void show(std::ostream & str); \
    void eval(EvalState & state, Env & env, Value & v);

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

struct ExprPath : Expr
{
    string s;
    ExprPath(const string & s) : s(s) { };
    COMMON_METHODS
};

struct ExprVar : Expr
{
    string name;
    ExprVar(const string & name) : name(name) { };
    COMMON_METHODS
};

struct ExprSelect : Expr
{
    Expr * e;
    string name;
    ExprSelect(Expr * e, const string & name) : e(e), name(name) { };
    COMMON_METHODS
};

struct ExprOpHasAttr : Expr
{
    Expr * e;
    string name;
    ExprOpHasAttr(Expr * e, const string & name) : e(e), name(name) { };
    COMMON_METHODS
};

struct ExprAttrs : Expr
{
    bool recursive;
    typedef std::map<string, Expr *> Attrs;
    Attrs attrs;
    list<string> inherited;
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
    string name;
    Expr * def;
    Formal(const string & name, Expr * def) : name(name), def(def) { };
};

struct Formals
{
    typedef std::list<Formal> Formals_;
    Formals_ formals;
    bool ellipsis;
};

struct ExprLambda : Expr
{
    Pos pos;
    string arg;
    bool matchAttrs;
    Formals * formals;
    Expr * body;
    ExprLambda(const Pos & pos, const string & arg, bool matchAttrs, Formals * formals, Expr * body)
        : pos(pos), arg(arg), matchAttrs(matchAttrs), formals(formals), body(body) { };
    COMMON_METHODS
};

struct ExprWith : Expr
{
    Pos pos;
    Expr * attrs, * body;
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


#if 0
/* Generic bottomup traversal over ATerms.  The traversal first
   recursively descends into subterms, and then applies the given term
   function to the resulting term. */
struct TermFun
{
    virtual ~TermFun() { }
    virtual ATerm operator () (ATerm e) = 0;
};
ATerm bottomupRewrite(TermFun & f, ATerm e);


/* Create an attribute set expression from an Attrs value. */
Expr makeAttrs(const ATermMap & attrs);


/* Check whether all variables are defined in the given expression.
   Throw an exception if this isn't the case. */
void checkVarDefs(const ATermMap & def, Expr e);
#endif


}


#endif /* !__NIXEXPR_H */
