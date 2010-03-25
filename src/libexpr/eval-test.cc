#include "nixexpr.hh"
#include "parser.hh"
#include "hash.hh"
#include "util.hh"
#include "nixexpr-ast.hh"

#include <cstdlib>

using namespace nix;


struct Env;
struct Value;

typedef ATerm Sym;

typedef std::map<Sym, Value> Bindings;


struct Env
{
    Env * up;
    Bindings bindings;
};


typedef enum {
    tInt = 1,
    tAttrs,
    tList,
    tThunk,
    tLambda,
    tCopy,
    tBlackhole
} ValueType;


struct Value
{
    ValueType type;
    union 
    {
        int integer;
        Bindings * attrs;
        struct {
            unsigned int length;
            Value * elems;
        } list;
        struct {
            Env * env;
            Expr expr;
        } thunk;
        struct {
            Env * env;
            Pattern pat;
            Expr body;
        } lambda;
        Value * val;
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
            str << aterm2String(i->first) << " = " << i->second << "; ";
        str << "}";
        break;
    case tList:
        str << "[ ";
        for (unsigned int n = 0; n < v.list.length; ++n)
            str << v.list.elems[n] << " ";
        str << "]";
        break;
    case tThunk:
        str << "<CODE>";
        break;
    case tLambda:
        str << "<LAMBDA>";
        break;
    default:
        abort();
    }
    return str;
}


static void eval(Env * env, Expr e, Value & v);


static void forceValue(Value & v)
{
    if (v.type == tThunk) {
        v.type = tBlackhole;
        eval(v.thunk.env, v.thunk.expr, v);
    }
    else if (v.type == tCopy) {
        forceValue(*v.val);
        v = *v.val;
    }
    else if (v.type == tBlackhole)
        throw EvalError("infinite recursion encountered");
}


static Value * lookupWith(Env * env, Sym name)
{
    if (!env) return 0;
    Value * v = lookupWith(env->up, name);
    if (v) return v;
    Bindings::iterator i = env->bindings.find(sWith);
    if (i == env->bindings.end()) return 0;
    Bindings::iterator j = i->second.attrs->find(name);
    if (j != i->second.attrs->end()) return &j->second;
    return 0;
}


static Value * lookupVar(Env * env, Sym name)
{
    /* First look for a regular variable binding for `name'. */
    for (Env * env2 = env; env2; env2 = env2->up) {
        Bindings::iterator i = env2->bindings.find(name);
        if (i != env2->bindings.end()) return &i->second;
    }

    /* Otherwise, look for a `with' attribute set containing `name'.
       Outer `withs' take precedence (i.e. `with {x=1;}; with {x=2;};
       x' evaluates to 1).  */
    Value * v = lookupWith(env, name);
    if (v) return v;

    /* Alternative implementation where the inner `withs' take
       precedence (i.e. `with {x=1;}; with {x=2;}; x' evaluates to
       2). */
#if 0
    for (Env * env2 = env; env2; env2 = env2->up) {
        Bindings::iterator i = env2->bindings.find(sWith);
        if (i == env2->bindings.end()) continue;
        Bindings::iterator j = i->second.attrs->find(name);
        if (j != i->second.attrs->end()) return &j->second;
    }
#endif
    
    throw Error("undefined variable");
}


unsigned long nrValues = 0, nrEnvs = 0;

static Env * allocEnv()
{
    nrEnvs++;
    return new Env;
}


char * p1 = 0, * p2 = 0;


static void eval(Env * env, Expr e, Value & v)
{
    char c;
    if (!p1) p1 = &c; else if (!p2) p2 = &c;

    printMsg(lvlError, format("eval: %1%") % e);

    Sym name;
    if (matchVar(e, name)) {
        Value * v2 = lookupVar(env, name);
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
            Value & v2 = (*v.attrs)[name];
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
            Value & v2 = env2->bindings[name];
            nrValues++;
            v2.type = tThunk;
            v2.thunk.env = env2;
            v2.thunk.expr = e2;
        }
        
        return;
    }

    Expr e1, e2;
    if (matchSelect(e, e2, name)) {
        eval(env, e2, v);
        if (v.type != tAttrs) throw TypeError("expected attribute set");
        Bindings::iterator i = v.attrs->find(name);
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

        Env * env2 = allocEnv();
        env2->up = env;

        ATermList formals; ATerm ellipsis;

        if (matchVarPat(v.lambda.pat, name)) {
            Value & vArg = env2->bindings[name];
            vArg.type = tThunk;
            vArg.thunk.env = env;
            vArg.thunk.expr = arg;
        }

        else if (matchAttrsPat(v.lambda.pat, formals, ellipsis, name)) {
            Value * vArg;
            Value vArg_;

            if (name == sNoAlias)
                vArg = &vArg_;
            else 
                vArg = &env2->bindings[name];

            eval(env, arg, *vArg);
            if (vArg->type != tAttrs) throw TypeError("expected attribute set");
            
            /* For each formal argument, get the actual argument.  If
               there is no matching actual argument but the formal
               argument has a default, use the default. */
            unsigned int attrsUsed = 0;
            for (ATermIterator i(formals); i; ++i) {
                Expr def; Sym name;
                DefaultValue def2;
                if (!matchFormal(*i, name, def2)) abort(); /* can't happen */

                Bindings::iterator j = vArg->attrs->find(name);
                
                Value & v = env2->bindings[name];
                
                if (j == vArg->attrs->end()) {
                    if (!matchDefaultValue(def2, def)) def = 0;
                    if (def == 0) throw TypeError(format("the argument named `%1%' required by the function is missing")
                        % aterm2String(name));
                    v.type = tThunk;
                    v.thunk.env = env2;
                    v.thunk.expr = def;
                } else {
                    attrsUsed++;
                    v.type = tCopy;
                    v.val = &j->second;
                }
            }

            /* Check that each actual argument is listed as a formal
               argument (unless the attribute match specifies a
               `...').  TODO: show the names of the
               expected/unexpected arguments. */
            if (ellipsis == eFalse && attrsUsed != vArg->attrs->size())
                throw TypeError("function called with unexpected argument");
        }

        else abort();
        
        eval(env2, v.lambda.body, v);
        return;
    }

    Expr attrs;
    if (matchWith(e, attrs, body, pos)) {
        Env * env2 = allocEnv();
        env2->up = env;

        Value & vAttrs = env2->bindings[sWith];
        eval(env, attrs, vAttrs);
        if (vAttrs.type != tAttrs) throw TypeError("`with' should evaluate to an attribute set");
        
        eval(env2, body, v);
        return;
    }

    if (matchList(e, es)) {
        v.type = tList;
        v.list.length = ATgetLength(es);
        v.list.elems = new Value[v.list.length]; // !!! check destructor
        for (unsigned int n = 0; n < v.list.length; ++n, es = ATgetNext(es)) {
            v.list.elems[n].type = tThunk;
            v.list.elems[n].thunk.env = env;
            v.list.elems[n].thunk.expr = ATgetFirst(es);
        }
        return;
    }

    if (matchOpConcat(e, e1, e2)) {
        Value v1; eval(env, e1, v1);
        if (v1.type != tList) throw TypeError("list expected");
        Value v2; eval(env, e2, v2);
        if (v2.type != tList) throw TypeError("list expected");
        v.type = tList;
        v.list.length = v1.list.length + v2.list.length;
        v.list.elems = new Value[v.list.length];
        /* !!! This loses sharing with the original lists.  We could
           use a tCopy node, but that would use more memory. */
        for (unsigned int n = 0; n < v1.list.length; ++n)
            v.list.elems[n] = v1.list.elems[n];
        for (unsigned int n = 0; n < v2.list.length; ++n)
            v.list.elems[n + v1.list.length] = v2.list.elems[n];
        return;
    }

    throw Error("unsupported term");
}


static void strictEval(Env * env, Expr e, Value & v)
{
    eval(env, e, v);
    
    if (v.type == tAttrs) {
        foreach (Bindings::iterator, i, *v.attrs)
            forceValue(i->second);
    }
    
    else if (v.type == tList) {
        for (unsigned int n = 0; n < v.list.length; ++n)
            forceValue(v.list.elems[n]);
    }
}


void doTest(string s)
{
    p1 = p2 = 0;
    EvalState state;
    Expr e = parseExprFromString(state, s, "/");
    printMsg(lvlError, format(">>>>> %1%") % e);
    Value v;
    strictEval(0, e, v);
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
    doTest("x: x");
    doTest("({x, y}: x) { x = 1; y = 2; }");
    doTest("({x, y}@args: args.x) { x = 1; y = 2; }");
    doTest("(args@{x, y}: args.x) { x = 1; y = 2; }");
    doTest("({x ? 1}: x) { }");
    doTest("({x ? 1, y ? x}: y) { x = 2; }");
    doTest("({x, y, ...}: x) { x = 1; y = 2; z = 3; }");
    doTest("({x, y, ...}@args: args.z) { x = 1; y = 2; z = 3; }");
    //doTest("({x ? y, y ? x}: y) { }");
    doTest("let x = 1; in x");
    doTest("with { x = 1; }; x");
    doTest("let x = 2; in with { x = 1; }; x"); // => 2
    doTest("with { x = 1; }; with { x = 2; }; x"); // => 1
    doTest("[ 1 2 3 ]");
    doTest("[ 1 2 ] ++ [ 3 4 5 ]");
    
    printMsg(lvlError, format("alloced %1% values") % nrValues);
    printMsg(lvlError, format("alloced %1% environments") % nrEnvs);
    printMsg(lvlError, format("each eval() uses %1% bytes of stack space") % (p1 - p2));
}


void printHelp()
{
}


string programId = "eval-test";
