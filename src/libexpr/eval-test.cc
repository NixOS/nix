#include "nixexpr.hh"
#include "parser.hh"
#include "hash.hh"
#include "util.hh"
#include "nixexpr-ast.hh"

#include <cstdlib>
#include <cstring>

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
    tBool,
    tString,
    tPath,
    tNull,
    tAttrs,
    tList,
    tThunk,
    tLambda,
    tCopy,
    tBlackhole,
    tPrimOp,
    tPrimOpApp,
} ValueType;


typedef void (* PrimOp_) (Value * * args, Value & v);


struct Value
{
    ValueType type;
    union 
    {
        int integer;
        bool boolean;
        struct {
            const char * s;
            const char * * context;
        } string;
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
        struct {
            PrimOp_ fun;
            unsigned int arity;
        } primOp;
        struct {
            Value * left, * right;
            unsigned int argsLeft;
        } primOpApp;
    };
};


static void mkThunk(Value & v, Env & env, Expr expr)
{
    v.type = tThunk;
    v.thunk.env = &env;
    v.thunk.expr = expr;
}


static void mkInt(Value & v, int n)
{
    v.type = tInt;
    v.integer = n;
}


static void mkBool(Value & v, bool b)
{
    v.type = tBool;
    v.boolean = b;
}


static void mkString(Value & v, const char * s)
{
    v.type = tString;
    v.string.s = s;
    v.string.context = 0;
}


std::ostream & operator << (std::ostream & str, Value & v)
{
    switch (v.type) {
    case tInt:
        str << v.integer;
        break;
    case tBool:
        str << (v.boolean ? "true" : "false");
        break;
    case tString:
        str << "\"" << v.string.s << "\""; // !!! escaping
        break;
    case tNull:
        str << "true";
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
    case tPrimOp:
        str << "<PRIMOP>";
        break;
    case tPrimOpApp:
        str << "<PRIMOP-APP>";
        break;
    default:
        abort();
    }
    return str;
}


static void eval(Env & env, Expr e, Value & v);


string showType(Value & v)
{
    switch (v.type) {
        case tString: return "a string";
        case tPath: return "a path";
        case tNull: return "null";
        case tInt: return "an integer";
        case tBool: return "a boolean";
        case tLambda: return "a function";
        case tAttrs: return "an attribute set";
        case tList: return "a list";
        case tPrimOpApp: return "a partially applied built-in function";
        default: throw Error("unknown type");
    }
}


static void forceValue(Value & v)
{
    if (v.type == tThunk) {
        v.type = tBlackhole;
        eval(*v.thunk.env, v.thunk.expr, v);
    }
    else if (v.type == tCopy) {
        forceValue(*v.val);
        v = *v.val;
    }
    else if (v.type == tBlackhole)
        throw EvalError("infinite recursion encountered");
}


static void forceInt(Value & v)
{
    forceValue(v);
    if (v.type != tInt)
        throw TypeError(format("value is %1% while an integer was expected") % showType(v));
}


static void forceAttrs(Value & v)
{
    forceValue(v);
    if (v.type != tAttrs)
        throw TypeError(format("value is %1% while an attribute set was expected") % showType(v));
}


static void forceList(Value & v)
{
    forceValue(v);
    if (v.type != tList)
        throw TypeError(format("value is %1% while a list was expected") % showType(v));
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


static bool eqValues(Value & v1, Value & v2)
{
    forceValue(v1);
    forceValue(v2);
    switch (v1.type) {

        case tInt:
            return v2.type == tInt && v1.integer == v2.integer;

        case tBool:
            return v2.type == tBool && v1.boolean == v2.boolean;

        case tList:
            if (v2.type != tList || v1.list.length != v2.list.length) return false;
            for (unsigned int n = 0; n < v1.list.length; ++n)
                if (!eqValues(v1.list.elems[n], v2.list.elems[n])) return false;
            return true;

        case tAttrs: {
            if (v2.type != tAttrs || v1.attrs->size() != v2.attrs->size()) return false;
            Bindings::iterator i, j;
            for (i = v1.attrs->begin(), j = v2.attrs->begin(); i != v1.attrs->end(); ++i, ++j)
                if (!eqValues(i->second, j->second)) return false;
            return true;
        }

        default:
            throw Error("cannot compare given values");
    }
}


unsigned long nrValues = 0, nrEnvs = 0, nrEvaluated = 0;

static Value * allocValues(unsigned int count)
{
    nrValues += count;
    return new Value[count]; // !!! check destructor
}

static Env & allocEnv()
{
    nrEnvs++;
    return *(new Env);
}


char * p1 = 0, * p2 = 0;


static bool evalBool(Env & env, Expr e)
{
    Value v;
    eval(env, e, v);
    if (v.type != tBool)
        throw TypeError(format("value is %1% while a Boolean was expected") % showType(v));
    return v.boolean;
}


static void eval(Env & env, Expr e, Value & v)
{
    char c;
    if (!p1) p1 = &c; else if (!p2) p2 = &c;

    printMsg(lvlError, format("eval: %1%") % e);

    nrEvaluated++;

    Sym name;
    if (matchVar(e, name)) {
        Value * v2 = lookupVar(&env, name);
        forceValue(*v2);
        v = *v2;
        return;
    }

    int n;
    if (matchInt(e, n)) {
        mkInt(v, n);
        return;
    }

    ATerm s; ATermList context;
    if (matchStr(e, s, context)) {
        assert(context == ATempty);
        mkString(v, ATgetName(ATgetAFun(s)));
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
            mkThunk(v2, env, e2);
        }
        return;
    }

    ATermList rbnds, nrbnds;
    if (matchRec(e, rbnds, nrbnds)) {
        Env & env2(allocEnv());
        env2.up = &env;
        
        v.type = tAttrs;
        v.attrs = &env2.bindings;
        ATerm name, e2, pos;
        for (ATermIterator i(rbnds); i; ++i) {
            if (!matchBind(*i, name, e2, pos)) abort(); /* can't happen */
            Value & v2 = env2.bindings[name];
            nrValues++;
            mkThunk(v2, env2, e2);
        }
        
        return;
    }

    Expr e1, e2;
    if (matchSelect(e, e2, name)) {
        eval(env, e2, v);
        forceAttrs(v); // !!! eval followed by force is slightly inefficient
        Bindings::iterator i = v.attrs->find(name);
        if (i == v.attrs->end()) throw TypeError("attribute not found");
        forceValue(i->second);
        v = i->second;
        return;
    }

    Pattern pat; Expr body; Pos pos;
    if (matchFunction(e, pat, body, pos)) {
        v.type = tLambda;
        v.lambda.env = &env;
        v.lambda.pat = pat;
        v.lambda.body = body;
        return;
    }

    Expr fun, arg;
    if (matchCall(e, fun, arg)) {
        eval(env, fun, v);

        if (v.type == tPrimOp || v.type == tPrimOpApp) {
            unsigned int argsLeft =
                v.type == tPrimOp ? v.primOp.arity : v.primOpApp.argsLeft;
            if (argsLeft == 1) {
                /* We have all the arguments, so call the primop.
                   First find the primop. */
                Value * primOp = &v;
                while (primOp->type == tPrimOpApp) primOp = primOp->primOpApp.left;
                assert(primOp->type == tPrimOp);
                unsigned int arity = primOp->primOp.arity;
                
                Value vLastArg;
                mkThunk(vLastArg, env, arg);

                /* Put all the arguments in an array. */
                Value * vArgs[arity];
                unsigned int n = arity - 1;
                vArgs[n--] = &vLastArg;
                for (Value * arg = &v; arg->type == tPrimOpApp; arg = arg->primOpApp.left)
                    vArgs[n--] = arg->primOpApp.right;

                /* And call the primop. */
                primOp->primOp.fun(vArgs, v);
            } else {
                Value * v2 = allocValues(2);
                v2[0] = v;
                mkThunk(v2[1], env, arg);
                v.type = tPrimOpApp;
                v.primOpApp.left = &v2[0];
                v.primOpApp.right = &v2[1];
                v.primOpApp.argsLeft = argsLeft - 1;
            }
            return;
        }
        
        if (v.type != tLambda) throw TypeError("expected function");

        Env & env2(allocEnv());
        env2.up = &env;

        ATermList formals; ATerm ellipsis;

        if (matchVarPat(v.lambda.pat, name)) {
            Value & vArg = env2.bindings[name];
            nrValues++;
            mkThunk(vArg, env, arg);
        }

        else if (matchAttrsPat(v.lambda.pat, formals, ellipsis, name)) {
            Value * vArg;
            Value vArg_;

            if (name == sNoAlias)
                vArg = &vArg_;
            else {
                vArg = &env2.bindings[name];
                nrValues++;
            }                

            eval(env, arg, *vArg);
            forceAttrs(*vArg);
            
            /* For each formal argument, get the actual argument.  If
               there is no matching actual argument but the formal
               argument has a default, use the default. */
            unsigned int attrsUsed = 0;
            for (ATermIterator i(formals); i; ++i) {
                Expr def; Sym name;
                DefaultValue def2;
                if (!matchFormal(*i, name, def2)) abort(); /* can't happen */

                Bindings::iterator j = vArg->attrs->find(name);
                
                Value & v = env2.bindings[name];
                nrValues++;
                
                if (j == vArg->attrs->end()) {
                    if (!matchDefaultValue(def2, def)) def = 0;
                    if (def == 0) throw TypeError(format("the argument named `%1%' required by the function is missing")
                        % aterm2String(name));
                    mkThunk(v, env2, def);
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
        Env & env2(allocEnv());
        env2.up = &env;

        Value & vAttrs = env2.bindings[sWith];
        nrValues++;
        eval(env, attrs, vAttrs);
        forceAttrs(vAttrs);
        
        eval(env2, body, v);
        return;
    }

    if (matchList(e, es)) {
        v.type = tList;
        v.list.length = ATgetLength(es);
        v.list.elems = allocValues(v.list.length);
        for (unsigned int n = 0; n < v.list.length; ++n, es = ATgetNext(es))
            mkThunk(v.list.elems[n], env, ATgetFirst(es));
        return;
    }

    if (matchOpEq(e, e1, e2)) {
        Value v1; eval(env, e1, v1);
        Value v2; eval(env, e2, v2);
        mkBool(v, eqValues(v1, v2));
        return;
    }

    if (matchOpNEq(e, e1, e2)) {
        Value v1; eval(env, e1, v1);
        Value v2; eval(env, e2, v2);
        mkBool(v, !eqValues(v1, v2));
        return;
    }

    if (matchOpConcat(e, e1, e2)) {
        Value v1; eval(env, e1, v1);
        forceList(v1);
        Value v2; eval(env, e2, v2);
        forceList(v2);
        v.type = tList;
        v.list.length = v1.list.length + v2.list.length;
        v.list.elems = allocValues(v.list.length);
        /* !!! This loses sharing with the original lists.  We could
           use a tCopy node, but that would use more memory. */
        for (unsigned int n = 0; n < v1.list.length; ++n)
            v.list.elems[n] = v1.list.elems[n];
        for (unsigned int n = 0; n < v2.list.length; ++n)
            v.list.elems[n + v1.list.length] = v2.list.elems[n];
        return;
    }

    if (matchConcatStrings(e, es)) {
        unsigned int n = ATgetLength(es), j = 0;
        Value vs[n];
        unsigned int len = 0;
        for (ATermIterator i(es); i; ++i, ++j) {
            eval(env, *i, vs[j]);
            if (vs[j].type != tString) throw TypeError("string expected");
            len += strlen(vs[j].string.s);
        }
        char * s = new char[len + 1], * t = s;
        for (unsigned int i = 0; i < j; ++i) {
            strcpy(t, vs[i].string.s);
            t += strlen(vs[i].string.s);
        }
        *t = 0;
        mkString(v, s);
        return;
    }

    Expr e3;
    if (matchIf(e, e1, e2, e3)) {
        eval(env, evalBool(env, e1) ? e2 : e3, v);
        return;
    }

    if (matchOpOr(e, e1, e2)) {
        mkBool(v, evalBool(env, e1) || evalBool(env, e2));
        return;
    }

    throw Error("unsupported term");
}


static void strictEval(Env & env, Expr e, Value & v)
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


static void prim_head(Value * * args, Value & v)
{
    forceList(*args[0]);
    if (args[0]->list.length == 0)
        throw Error("`head' called on an empty list");
    forceValue(args[0]->list.elems[0]);
    v = args[0]->list.elems[0];
}


static void prim_add(Value * * args, Value & v)
{
    forceInt(*args[0]);
    forceInt(*args[1]);
    mkInt(v, args[0]->integer + args[1]->integer);
}


static void addPrimOp(Env & env, const string & name, unsigned int arity, PrimOp_ fun)
{
    Value & v = env.bindings[toATerm(name)];
    nrValues++;
    v.type = tPrimOp;
    v.primOp.arity = arity;
    v.primOp.fun = fun;
}


void doTest(string s)
{
    Env baseEnv;
    baseEnv.up = 0;

    /* Add global constants such as `true' to the base environment. */
    {
        Value & v = baseEnv.bindings[toATerm("true")];
        v.type = tBool;
        v.boolean = true;
    }
    {
        Value & v = baseEnv.bindings[toATerm("false")];
        v.type = tBool;
        v.boolean = false;
    }
    {
        Value & v = baseEnv.bindings[toATerm("null")];
        v.type = tNull;
    }

    /* Add primops to the base environment. */
    addPrimOp(baseEnv, "__head", 1, prim_head);
    addPrimOp(baseEnv, "__add", 2, prim_add);
    
    p1 = p2 = 0;
    EvalState state;
    Expr e = parseExprFromString(state, s, "/");
    printMsg(lvlError, format(">>>>> %1%") % e);
    Value v;
    strictEval(baseEnv, e, v);
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
    doTest("123 == 123");
    doTest("123 == 456");
    doTest("let id = x: x; in [1 2] == [(id 1) (id 2)]");
    doTest("let id = x: x; in [1 2] == [(id 1) (id 3)]");
    doTest("[1 2] == [3 (let x = x; in x)]");
    doTest("{ x = 1; y.z = 2; } == { y = { z = 2; }; x = 1; }");
    doTest("{ x = 1; y = 2; } == { x = 2; }");
    doTest("{ x = [ 1 2 ]; } == { x = [ 1 ] ++ [ 2 ]; }");
    doTest("1 != 1");
    doTest("true");
    doTest("true == false");
    doTest("__head [ 1 2 3 ]");
    doTest("__add 1 2");
    doTest("null");
    doTest("\"foo\"");
    doTest("let s = \"bar\"; in \"foo${s}\"");
    doTest("if true then 1 else 2");
    doTest("if false then 1 else 2");
    doTest("if false || true then 1 else 2");
    doTest("let x = x; in if true || x then 1 else 2");
    
    printMsg(lvlError, format("alloced %1% values") % nrValues);
    printMsg(lvlError, format("alloced %1% environments") % nrEnvs);
    printMsg(lvlError, format("evaluated %1% expressions") % nrEvaluated);
    printMsg(lvlError, format("each eval() uses %1% bytes of stack space") % (p1 - p2));
}


void printHelp()
{
}


string programId = "eval-test";
