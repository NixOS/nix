#include "nixexpr.hh"
#include "parser.hh"
#include "hash.hh"
#include "util.hh"
#include "nixexpr-ast.hh"

#include <cstdlib>

using namespace nix;


struct Env;
struct Value;

typedef std::map<string, Value> Bindings;


struct Env
{
    Env * up;
    Bindings bindings;
};


typedef enum {
    tInt = 1,
    tAttrs,
    tThunk,
    tLambda
} ValueType;


struct Value
{
    ValueType type;
    union 
    {
        int integer;
        Bindings * attrs;
        struct {
            Env * env;
            Expr expr;
        } thunk;
        struct {
            Env * env;
            Pattern pat;
            Expr body;
        } lambda;
    };
};


std::ostream & operator << (std::ostream & str, Value & v)
{
    switch (v.type) {
    case tInt:
        str << v.integer;
        break;
    case tAttrs:
        str << "{ ";
        foreach (Bindings::iterator, i, *v.attrs)
            str << i->first << " = " << i->second << "; ";
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


void eval(Env * env, Expr e, Value & v);


void forceValue(Value & v)
{
    if (v.type != tThunk) return;
    eval(v.thunk.env, v.thunk.expr, v);
}


Value * lookupVar(Env * env, const string & name)
{
    for ( ; env; env = env->up) {
        Bindings::iterator i = env->bindings.find(name);
        if (i != env->bindings.end()) return &i->second;
    }
    throw Error("undefined variable");
}


unsigned long nrValues = 0;

unsigned long nrEnvs = 0;

Env * allocEnv()
{
    nrEnvs++;
    return new Env;
}


void eval(Env * env, Expr e, Value & v)
{
    printMsg(lvlError, format("eval: %1%") % e);

    ATerm name;
    if (matchVar(e, name)) {
        Value * v2 = lookupVar(env, aterm2String(name));
        forceValue(*v2);
        v = *v2;
        return;
    }

    int n;
    if (matchInt(e, n)) {
        v.type = tInt;
        v.integer = n;
        return;
    }

    ATermList es;
    if (matchAttrs(e, es)) {
        v.type = tAttrs;
        v.attrs = new Bindings;
        ATerm e2, pos;
        for (ATermIterator i(es); i; ++i) {
            if (!matchBind(*i, name, e2, pos)) abort(); /* can't happen */
            Value & v2 = (*v.attrs)[aterm2String(name)];
            nrValues++;
            v2.type = tThunk;
            v2.thunk.env = env;
            v2.thunk.expr = e2;
        }
        return;
    }

    ATermList rbnds, nrbnds;
    if (matchRec(e, rbnds, nrbnds)) {
        Env * env2 = allocEnv();
        env2->up = env;
        
        v.type = tAttrs;
        v.attrs = &env2->bindings;
        ATerm name, e2, pos;
        for (ATermIterator i(rbnds); i; ++i) {
            if (!matchBind(*i, name, e2, pos)) abort(); /* can't happen */
            Value & v2 = env2->bindings[aterm2String(name)];
            nrValues++;
            v2.type = tThunk;
            v2.thunk.env = env2;
            v2.thunk.expr = e2;
        }
        
        return;
    }

    Expr e2;
    if (matchSelect(e, e2, name)) {
        eval(env, e2, v);
        if (v.type != tAttrs) throw TypeError("expected attribute set");
        Bindings::iterator i = v.attrs->find(aterm2String(name));
        if (i == v.attrs->end()) throw TypeError("attribute not found");
        forceValue(i->second);
        v = i->second;
        return;
    }

    Pattern pat; Expr body; Pos pos;
    if (matchFunction(e, pat, body, pos)) {
        v.type = tLambda;
        v.lambda.env = env;
        v.lambda.pat = pat;
        v.lambda.body = body;
        return;
    }

    Expr fun, arg;
    if (matchCall(e, fun, arg)) {
        eval(env, fun, v);
        if (v.type != tLambda) throw TypeError("expected function");
        if (!matchVarPat(v.lambda.pat, name)) throw Error("not implemented");

        Env * env2 = allocEnv();
        env2->up = env;
        
        Value & arg_ = env2->bindings[aterm2String(name)];
        nrValues++;
        arg_.type = tThunk;
        arg_.thunk.env = env;
        arg_.thunk.expr = arg;

        eval(env2, v.lambda.body, v);
        return;
    }

    abort();
}


void doTest(string s)
{
    EvalState state;
    Expr e = parseExprFromString(state, s, "/");
    printMsg(lvlError, format("%1%") % e);
    Value v;
    eval(0, e, v);
    printMsg(lvlError, format("result: %1%") % v);
}


void run(Strings args)
{
    printMsg(lvlError, format("size of value: %1% bytes") % sizeof(Value));
    
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

    printMsg(lvlError, format("alloced %1% values") % nrValues);
    printMsg(lvlError, format("alloced %1% environments") % nrEnvs);
}


void printHelp()
{
}


string programId = "eval-test";
