#include "nixexpr.hh"
#include "parser.hh"
#include "hash.hh"
#include "util.hh"
#include "nixexpr-ast.hh"

#include <cstdlib>

using namespace nix;


typedef struct Env_ * Env;
typedef struct Value_ * Value;

typedef std::map<string, Value> Bindings;


struct Env_
{
    Env up;
    Bindings bindings;
};


typedef enum {
    tInt = 1,
    tAttrs,
    tThunk,
    tLambda
} ValueType;


struct Value_
{
    ValueType type;
    union 
    {
        int integer;
        Bindings * attrs;
        struct {
            Env env;
            Expr expr;
        } thunk;
        struct {
            Env env;
            Pattern pat;
            Expr body;
        } lambda;
    };
};


std::ostream & operator << (std::ostream & str, Value_ & v)
{
    switch (v.type) {
    case tInt:
        str << v.integer;
        break;
    case tAttrs:
        str << "{ ";
        foreach (Bindings::iterator, i, *v.attrs) {
            str << i->first << " = " << *i->second << "; ";
        }
        str << "}";
        break;
    case tThunk:
        str << "<CODE>";
        break;
    default:
        abort();
    }
    return str;
}


Value eval(Env env, Expr e);


void forceValue(Value v)
{
    if (v->type != tThunk) return;
    Value v2 = eval(v->thunk.env, v->thunk.expr);
    *v = *v2; // !!! slightly inefficient
}


Value lookupVar(Env env, const string & name)
{
    for ( ; env; env = env->up) {
        Value v = env->bindings[name];
        if (v) return v;
    }
    throw Error("undefined variable");
}


Value eval(Env env, Expr e)
{
    printMsg(lvlError, format("eval: %1%") % e);

    ATerm name;
    if (matchVar(e, name)) {
        Value v = lookupVar(env, aterm2String(name));
        forceValue(v);
        return v;
    }

    int n;
    if (matchInt(e, n)) {
        Value v = new Value_;
        v->type = tInt;
        v->integer = n;
        return v;
    }

    ATermList es;
    if (matchAttrs(e, es)) {
        Value v = new Value_;
        v->type = tAttrs;
        v->attrs = new Bindings;
        ATerm e2, pos;
        for (ATermIterator i(es); i; ++i) {
            if (!matchBind(*i, name, e2, pos)) abort(); /* can't happen */
            Value v2 = new Value_;
            v2->type = tThunk;
            v2->thunk.env = env;
            v2->thunk.expr = e2;
            (*v->attrs)[aterm2String(name)] = v2;
        }
        return v;
    }

    ATermList rbnds, nrbnds;
    if (matchRec(e, rbnds, nrbnds)) {
        Env env2 = new Env_;
        env2->up = env;
        
        Value v = new Value_;
        v->type = tAttrs;
        v->attrs = &env2->bindings;
        ATerm name, e2, pos;
        for (ATermIterator i(rbnds); i; ++i) {
            if (!matchBind(*i, name, e2, pos)) abort(); /* can't happen */
            Value v2 = new Value_;
            v2->type = tThunk;
            v2->thunk.env = env2;
            v2->thunk.expr = e2;
            env2->bindings[aterm2String(name)] = v2;
        }
        return v;
    }

    Expr e2;
    if (matchSelect(e, e2, name)) {
        Value v = eval(env, e2);
        if (v->type != tAttrs) throw TypeError("expected attribute set");
        Value v2 = (*v->attrs)[aterm2String(name)];
        if (!v2) throw TypeError("attribute not found");
        forceValue(v2);
        return v2;
    }

    Pattern pat; Expr body; Pos pos;
    if (matchFunction(e, pat, body, pos)) {
        Value v = new Value_;
        v->type = tLambda;
        v->lambda.env = env;
        v->lambda.pat = pat;
        v->lambda.body = body;
        return v;
    }

    Expr fun, arg;
    if (matchCall(e, fun, arg)) {
        Value fun_ = eval(env, fun);
        if (fun_->type != tLambda) throw TypeError("expected function");
        if (!matchVarPat(fun_->lambda.pat, name)) throw Error("not implemented");

        Value arg_ = new Value_;
        arg_->type = tThunk;
        arg_->thunk.env = env;
        arg_->thunk.expr = arg;
        
        Env env2 = new Env_;
        env2->up = env;
        env2->bindings[aterm2String(name)] = arg_;

        return eval(env2, fun_->lambda.body);
    }

    abort();
}


void doTest(string s)
{
    EvalState state;
    Expr e = parseExprFromString(state, s, "/");
    printMsg(lvlError, format("%1%") % e);
    Value v = eval(0, e);
    printMsg(lvlError, format("result: %1%") % *v);
}


void run(Strings args)
{
    printMsg(lvlError, format("size of value: %1% bytes") % sizeof(Value_));
    
    doTest("123");
    doTest("{ x = 1; y = 2; }");
    doTest("{ x = 1; y = 2; }.y");
    doTest("rec { x = 1; y = x; }.y");
    doTest("(x: x) 1");
    doTest("(x: y: y) 1 2");
    //Expr e = parseExprFromString(state, "let x = \"a\"; in x + \"b\"", "/");
    //Expr e = parseExprFromString(state, "(x: x + \"b\") \"a\"", "/");
    //Expr e = parseExprFromString(state, "\"a\" + \"b\"", "/");
    //Expr e = parseExprFromString(state, "\"a\" + \"b\"", "/");
}


void printHelp()
{
}


string programId = "eval-test";
