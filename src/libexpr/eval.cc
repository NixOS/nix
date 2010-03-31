#include "eval.hh"
#include "parser.hh"
#include "hash.hh"
#include "util.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "nixexpr-ast.hh"
#include "globals.hh"

#include <cstring>


#define LocalNoInline(f) static f __attribute__((noinline)); f
#define LocalNoInlineNoReturn(f) static f __attribute__((noinline, noreturn)); f


namespace nix {
    

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
    case tPath:
        str << v.path; // !!! escaping?
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
        throw Error("invalid value");
    }
    return str;
}


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


EvalState::EvalState() : baseEnv(allocEnv())
{
    nrValues = nrEnvs = nrEvaluated = 0;

    initNixExprHelpers();

    createBaseEnv();
    
    allowUnsafeEquality = getEnv("NIX_NO_UNSAFE_EQ", "") == "";
}


void EvalState::addConstant(const string & name, Value & v)
{
    baseEnv.bindings[toATerm(name)] = v;
    string name2 = string(name, 0, 2) == "__" ? string(name, 2) : name;
    (*baseEnv.bindings[toATerm("builtins")].attrs)[toATerm(name2)] = v;
    nrValues += 2;
}


void EvalState::addPrimOp(const string & name,
    unsigned int arity, PrimOp primOp)
{
    Value v;
    v.type = tPrimOp;
    v.primOp.arity = arity;
    v.primOp.fun = primOp;
    baseEnv.bindings[toATerm(name)] = v;
    string name2 = string(name, 0, 2) == "__" ? string(name, 2) : name;
    (*baseEnv.bindings[toATerm("builtins")].attrs)[toATerm(name2)] = v;
    nrValues += 2;
}


/* Every "format" object (even temporary) takes up a few hundred bytes
   of stack space, which is a real killer in the recursive
   evaluator.  So here are some helper functions for throwing
   exceptions. */

LocalNoInlineNoReturn(void throwEvalError(const char * s))
{
    throw EvalError(s);
}

LocalNoInlineNoReturn(void throwEvalError(const char * s, const string & s2))
{
    throw EvalError(format(s) % s2);
}

LocalNoInlineNoReturn(void throwTypeError(const char * s))
{
    throw TypeError(s);
}

LocalNoInlineNoReturn(void throwTypeError(const char * s, const string & s2))
{
    throw TypeError(format(s) % s2);
}

LocalNoInline(void addErrorPrefix(Error & e, const char * s))
{
    e.addPrefix(s);
}

LocalNoInline(void addErrorPrefix(Error & e, const char * s, const string & s2))
{
    e.addPrefix(format(s) % s2);
}

LocalNoInline(void addErrorPrefix(Error & e, const char * s, const string & s2, const string & s3))
{
    e.addPrefix(format(s) % s2 % s3);
}


static void mkThunk(Value & v, Env & env, Expr expr)
{
    v.type = tThunk;
    v.thunk.env = &env;
    v.thunk.expr = expr;
}


void mkString(Value & v, const char * s)
{
    v.type = tString;
    v.string.s = strdup(s);
    v.string.context = 0;
}


void mkString(Value & v, const string & s, const PathSet & context)
{
    mkString(v, s.c_str());
    if (!context.empty()) {
        unsigned int len = 0, n = 0;
        v.string.context = new const char *[context.size() + 1];
        foreach (PathSet::const_iterator, i, context) 
            v.string.context[n++] = strdup(i->c_str());
        v.string.context[n] = 0;
    }
}


void mkPath(Value & v, const char * s)
{
    v.type = tPath;
    v.path = strdup(s);
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
    
    throw Error(format("undefined variable `%1%'") % aterm2String(name));
}


Value * EvalState::allocValues(unsigned int count)
{
    nrValues += count;
    return new Value[count]; // !!! check destructor
}


Env & EvalState::allocEnv()
{
    nrEnvs++;
    return *(new Env);
}


void EvalState::mkList(Value & v, unsigned int length)
{
    v.type = tList;
    v.list.length = length;
    v.list.elems = allocValues(length);
}


void EvalState::mkAttrs(Value & v)
{
    v.type = tAttrs;
    v.attrs = new Bindings;
}


void EvalState::cloneAttrs(Value & src, Value & dst)
{
    mkAttrs(dst);
    foreach (Bindings::iterator, i, *src.attrs)
        (*dst.attrs)[i->first] = i->second; // !!! sharing?
}


void EvalState::evalFile(const Path & path, Value & v)
{
    startNest(nest, lvlTalkative, format("evaluating file `%1%'") % path);

    Expr e = parseTrees.get(toATerm(path));

    if (!e) {
        e = parseExprFromFile(*this, path);
        parseTrees.set(toATerm(path), e);
    }
    
    try {
        eval(e, v);
    } catch (Error & e) {
        e.addPrefix(format("while evaluating the file `%1%':\n")
            % path);
        throw;
    }
}


static char * deepestStack = (char *) -1; /* for measuring stack usage */


void EvalState::eval(Env & env, Expr e, Value & v)
{
    /* When changing this function, make sure that you don't cause a
       (large) increase in stack consumption! */
    
    char x;
    if (&x < deepestStack) deepestStack = &x;
    
    debug(format("eval: %1%") % e);

    nrEvaluated++;

    Sym name;
    int n;
    ATerm s; ATermList context, es;
    ATermList rbnds, nrbnds;
    Expr e1, e2, e3, fun, arg, attrs;
    Pattern pat; Expr body; Pos pos;
    
    if (matchVar(e, name)) {
        Value * v2 = lookupVar(&env, name);
        forceValue(*v2);
        v = *v2;
    }

    else if (matchInt(e, n))
        mkInt(v, n);

    else if (matchStr(e, s, context)) {
        assert(context == ATempty);
        mkString(v, ATgetName(ATgetAFun(s)));
    }

    else if (matchPath(e, s))
        mkPath(v, ATgetName(ATgetAFun(s)));

    else if (matchAttrs(e, es)) {
        mkAttrs(v);
        ATerm e2, pos;
        for (ATermIterator i(es); i; ++i) {
            if (!matchBind(*i, name, e2, pos)) abort(); /* can't happen */
            Value & v2 = (*v.attrs)[name];
            nrValues++;
            mkThunk(v2, env, e2);
        }
    }

    else if (matchRec(e, rbnds, nrbnds)) {
        /* Create a new environment that contains the attributes in
           this `rec'. */
        Env & env2(allocEnv());
        env2.up = &env;
        
        v.type = tAttrs;
        v.attrs = &env2.bindings;

        /* The recursive attributes are evaluated in the new
           environment. */
        ATerm name, e2, pos;
        for (ATermIterator i(rbnds); i; ++i) {
            if (!matchBind(*i, name, e2, pos)) abort(); /* can't happen */
            Value & v2 = env2.bindings[name];
            nrValues++;
            mkThunk(v2, env2, e2);
        }

        /* The non-recursive attributes, on the other hand, are
           evaluated in the original environment. */
        for (ATermIterator i(nrbnds); i; ++i) {
            if (!matchBind(*i, name, e2, pos)) abort(); /* can't happen */
            Value & v2 = env2.bindings[name];
            nrValues++;
            mkThunk(v2, env, e2);
        }
    }

    else if (matchSelect(e, e2, name)) {
        eval(env, e2, v);
        forceAttrs(v); // !!! eval followed by force is slightly inefficient
        Bindings::iterator i = v.attrs->find(name);
        if (i == v.attrs->end())
            throwEvalError("attribute `%1%' missing", aterm2String(name));
        try {            
            forceValue(i->second);
        } catch (Error & e) {
            addErrorPrefix(e, "while evaluating the attribute `%1%':\n", aterm2String(name));
            throw;
        }
        v = i->second;
    }

    else if (matchFunction(e, pat, body, pos)) {
        v.type = tLambda;
        v.lambda.env = &env;
        v.lambda.pat = pat;
        v.lambda.body = body;
    }

    else if (matchCall(e, fun, arg)) {
        eval(env, fun, v);
        Value vArg;
        mkThunk(vArg, env, arg); // !!! should this be on the heap?
        callFunction(v, vArg, v);
    }

    else if (matchWith(e, attrs, body, pos)) {
        Env & env2(allocEnv());
        env2.up = &env;

        Value & vAttrs = env2.bindings[sWith];
        nrValues++;
        eval(env, attrs, vAttrs);
        forceAttrs(vAttrs);
        
        eval(env2, body, v);
    }

    else if (matchList(e, es)) {
        mkList(v, ATgetLength(es));
        for (unsigned int n = 0; n < v.list.length; ++n, es = ATgetNext(es))
            mkThunk(v.list.elems[n], env, ATgetFirst(es));
    }

    else if (matchOpEq(e, e1, e2)) {
        Value v1; eval(env, e1, v1);
        Value v2; eval(env, e2, v2);
        mkBool(v, eqValues(v1, v2));
    }

    else if (matchOpNEq(e, e1, e2)) {
        Value v1; eval(env, e1, v1);
        Value v2; eval(env, e2, v2);
        mkBool(v, !eqValues(v1, v2));
    }

    else if (matchOpConcat(e, e1, e2)) {
        Value v1; eval(env, e1, v1);
        forceList(v1);
        Value v2; eval(env, e2, v2);
        forceList(v2);
        mkList(v, v1.list.length + v2.list.length);
        /* !!! This loses sharing with the original lists.  We could
           use a tCopy node, but that would use more memory. */
        for (unsigned int n = 0; n < v1.list.length; ++n)
            v.list.elems[n] = v1.list.elems[n];
        for (unsigned int n = 0; n < v2.list.length; ++n)
            v.list.elems[n + v1.list.length] = v2.list.elems[n];
    }

    else if (matchConcatStrings(e, es)) {
        PathSet context;
        std::ostringstream s;
        
        bool first = true, isPath = false;
        
        for (ATermIterator i(es); i; ++i) {
            eval(env, *i, v);

            /* If the first element is a path, then the result will
               also be a path, we don't copy anything (yet - that's
               done later, since paths are copied when they are used
               in a derivation), and none of the strings are allowed
               to have contexts. */
            if (first) {
                isPath = v.type == tPath;
                first = false;
            }
            
            s << coerceToString(v, context, false, !isPath);
        }
        
        if (isPath && !context.empty())
            throw EvalError(format("a string that refers to a store path cannot be appended to a path, in `%1%'")
                % s.str());

        if (isPath)
            mkPath(v, s.str().c_str());
        else
            mkString(v, s.str(), context);
    }

    /* Conditionals. */
    else if (matchIf(e, e1, e2, e3))
        eval(env, evalBool(env, e1) ? e2 : e3, v);

    /* Assertions. */
    else if (matchAssert(e, e1, e2, pos)) {
        if (!evalBool(env, e1))
            throw AssertionError(format("assertion failed at %1%") % showPos(pos));
        eval(env, e2, v);
    }

    /* Negation. */
    else if (matchOpNot(e, e1))
        mkBool(v, !evalBool(env, e1));

    /* Implication. */
    else if (matchOpImpl(e, e1, e2))
        return mkBool(v, !evalBool(env, e1) || evalBool(env, e2));
    
    /* Conjunction (logical AND). */
    else if (matchOpAnd(e, e1, e2))
        mkBool(v, evalBool(env, e1) && evalBool(env, e2));
    
    /* Disjunction (logical OR). */
    else if (matchOpOr(e, e1, e2))
        mkBool(v, evalBool(env, e1) || evalBool(env, e2));

    /* Attribute set update (//). */
    else if (matchOpUpdate(e, e1, e2)) {
        Value v2;
        eval(env, e1, v2);
        
        cloneAttrs(v2, v);
        
        eval(env, e2, v2);
        foreach (Bindings::iterator, i, *v2.attrs)
            (*v.attrs)[i->first] = i->second; // !!! sharing
    }

    /* Attribute existence test (?). */
    else if (matchOpHasAttr(e, e1, name)) {
        eval(env, e1, v);
        forceAttrs(v);
        mkBool(v, v.attrs->find(name) != v.attrs->end());
    }

    else throw Error("unsupported term");
}


void EvalState::callFunction(Value & fun, Value & arg, Value & v)
{
    if (fun.type == tPrimOp || fun.type == tPrimOpApp) {
        unsigned int argsLeft =
            fun.type == tPrimOp ? fun.primOp.arity : fun.primOpApp.argsLeft;
        if (argsLeft == 1) {
            /* We have all the arguments, so call the primop.  First
               find the primop. */
            Value * primOp = &fun;
            while (primOp->type == tPrimOpApp) primOp = primOp->primOpApp.left;
            assert(primOp->type == tPrimOp);
            unsigned int arity = primOp->primOp.arity;
                
            /* Put all the arguments in an array. */
            Value * vArgs[arity];
            unsigned int n = arity - 1;
            vArgs[n--] = &arg;
            for (Value * arg = &fun; arg->type == tPrimOpApp; arg = arg->primOpApp.left)
                vArgs[n--] = arg->primOpApp.right;

            /* And call the primop. */
            primOp->primOp.fun(*this, vArgs, v);
        } else {
            Value * v2 = allocValues(2);
            v2[0] = fun;
            v2[1] = arg;
            v.type = tPrimOpApp;
            v.primOpApp.left = &v2[0];
            v.primOpApp.right = &v2[1];
            v.primOpApp.argsLeft = argsLeft - 1;
        }
        return;
    }
    
    if (fun.type != tLambda)
        throwTypeError("attempt to call something which is neither a function nor a primop (built-in operation) but %1%",
            showType(fun));

    Env & env2(allocEnv());
    env2.up = fun.lambda.env;

    ATermList formals; ATerm ellipsis, name;

    if (matchVarPat(fun.lambda.pat, name)) {
        Value & vArg = env2.bindings[name];
        nrValues++;
        vArg = arg;
    }

    else if (matchAttrsPat(fun.lambda.pat, formals, ellipsis, name)) {
        forceAttrs(arg);
        
        if (name != sNoAlias) {
            env2.bindings[name] = arg;
            nrValues++;
        }                

        /* For each formal argument, get the actual argument.  If
           there is no matching actual argument but the formal
           argument has a default, use the default. */
        unsigned int attrsUsed = 0;
        for (ATermIterator i(formals); i; ++i) {
            Expr def; Sym name;
            DefaultValue def2;
            if (!matchFormal(*i, name, def2)) abort(); /* can't happen */

            Bindings::iterator j = arg.attrs->find(name);
                
            Value & v = env2.bindings[name];
            nrValues++;
                
            if (j == arg.attrs->end()) {
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
           argument (unless the attribute match specifies a `...').
           TODO: show the names of the expected/unexpected
           arguments. */
        if (ellipsis == eFalse && attrsUsed != arg.attrs->size())
            throw TypeError("function called with unexpected argument");
    }

    else abort();
        
    eval(env2, fun.lambda.body, v);
}


void EvalState::eval(Expr e, Value & v)
{
    eval(baseEnv, e, v);
}


bool EvalState::evalBool(Env & env, Expr e)
{
    Value v;
    eval(env, e, v);
    if (v.type != tBool)
        throw TypeError(format("value is %1% while a Boolean was expected") % showType(v));
    return v.boolean;
}


void EvalState::strictEval(Env & env, Expr e, Value & v)
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


void EvalState::strictEval(Expr e, Value & v)
{
    strictEval(baseEnv, e, v);
}


void EvalState::forceValue(Value & v)
{
    if (v.type == tThunk) {
        v.type = tBlackhole;
        eval(*v.thunk.env, v.thunk.expr, v);
    }
    else if (v.type == tCopy) {
        forceValue(*v.val);
        v = *v.val;
    }
    else if (v.type == tApp)
        callFunction(*v.app.left, *v.app.right, v);
    else if (v.type == tBlackhole)
        throw EvalError("infinite recursion encountered");
}


int EvalState::forceInt(Value & v)
{
    forceValue(v);
    if (v.type != tInt)
        throw TypeError(format("value is %1% while an integer was expected") % showType(v));
    return v.integer;
}


bool EvalState::forceBool(Value & v)
{
    forceValue(v);
    if (v.type != tBool)
        throw TypeError(format("value is %1% while a Boolean was expected") % showType(v));
    return v.boolean;
}


void EvalState::forceAttrs(Value & v)
{
    forceValue(v);
    if (v.type != tAttrs)
        throw TypeError(format("value is %1% while an attribute set was expected") % showType(v));
}


void EvalState::forceList(Value & v)
{
    forceValue(v);
    if (v.type != tList)
        throw TypeError(format("value is %1% while a list was expected") % showType(v));
}


void EvalState::forceFunction(Value & v)
{
    forceValue(v);
    if (v.type != tLambda && v.type != tPrimOp && v.type != tPrimOpApp)
        throw TypeError(format("value is %1% while a function was expected") % showType(v));
}


string EvalState::forceString(Value & v)
{
    forceValue(v);
    if (v.type != tString)
        throw TypeError(format("value is %1% while a string was expected") % showType(v));
    return string(v.string.s);
}


string EvalState::forceString(Value & v, PathSet & context)
{
    string s = forceString(v);
    if (v.string.context) {
        for (const char * * p = v.string.context; *p; ++p) 
            context.insert(*p);
    }
    return s;
}


string EvalState::forceStringNoCtx(Value & v)
{
    string s = forceString(v);
    if (v.string.context)
        throw EvalError(format("the string `%1%' is not allowed to refer to a store path (such as `%2%')")
            % v.string.s % v.string.context[0]);
    return s;
}


string EvalState::coerceToString(Value & v, PathSet & context,
    bool coerceMore, bool copyToStore)
{
    forceValue(v);

    string s;

    if (v.type == tString) {
        if (v.string.context) 
            for (const char * * p = v.string.context; *p; ++p) 
                context.insert(*p);
        return v.string.s;
    }

    if (v.type == tPath) {
        Path path(canonPath(v.path));

        if (!copyToStore) return path;
        
        if (isDerivation(path))
            throw EvalError(format("file names are not allowed to end in `%1%'")
                % drvExtension);

        Path dstPath;
        if (srcToStore[path] != "")
            dstPath = srcToStore[path];
        else {
            dstPath = readOnlyMode
                ? computeStorePathForPath(path).first
                : store->addToStore(path);
            srcToStore[path] = dstPath;
            printMsg(lvlChatty, format("copied source `%1%' -> `%2%'")
                % path % dstPath);
        }

        context.insert(dstPath);
        return dstPath;
    }

    if (v.type == tAttrs) {
        Bindings::iterator i = v.attrs->find(toATerm("outPath"));
        if (i == v.attrs->end())
            throwTypeError("cannot coerce an attribute set (except a derivation) to a string");
        return coerceToString(i->second, context, coerceMore, copyToStore);
    }

    if (coerceMore) {

        /* Note that `false' is represented as an empty string for
           shell scripting convenience, just like `null'. */
        if (v.type == tBool && v.boolean) return "1";
        if (v.type == tBool && !v.boolean) return "";
        if (v.type == tInt) return int2String(v.integer);
        if (v.type == tNull) return "";

        if (v.type == tList) {
            string result;
            for (unsigned int n = 0; n < v.list.length; ++n) {
                if (n) result += " ";
                result += coerceToString(v.list.elems[n],
                    context, coerceMore, copyToStore);
            }
            return result;
        }
    }
    
    throwTypeError("cannot coerce %1% to a string", showType(v));
}


Path EvalState::coerceToPath(Value & v, PathSet & context)
{
    string path = coerceToString(v, context, false, false);
    if (path == "" || path[0] != '/')
        throw EvalError(format("string `%1%' doesn't represent an absolute path") % path);
    return path;
}


bool EvalState::eqValues(Value & v1, Value & v2)
{
    forceValue(v1);
    forceValue(v2);

    if (v1.type != v2.type) return false;
    
    switch (v1.type) {

        case tInt:
            return v1.integer == v2.integer;

        case tBool:
            return v1.boolean == v2.boolean;

        case tString:
            /* !!! contexts */
            return strcmp(v1.string.s, v2.string.s) == 0;

        case tPath:
            return strcmp(v1.path, v2.path) == 0;

        case tNull:
            return true;

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
            throw Error(format("cannot compare %1% with %2%") % showType(v1) % showType(v2));
    }
}


#if 0
/* Pattern-match `pat' against `arg'.  The result is a set of
   substitutions (`subs') and a set of recursive substitutions
   (`subsRecursive').  The latter can refer to the variables bound by
   both `subs' and `subsRecursive'. */
static void patternMatch(EvalState & state,
    Pattern pat, Expr arg, ATermMap & subs, ATermMap & subsRecursive)
{
    ATerm name;
    ATermList formals;
    ATermBool ellipsis;
    
    if (matchVarPat(pat, name)) 
        subs.set(name, arg);

    else if (matchAttrsPat(pat, formals, ellipsis, name)) {

        arg = evalExpr(state, arg);

        if (name != sNoAlias) subs.set(name, arg);

        /* Get the actual arguments. */
        ATermMap attrs;
        queryAllAttrs(arg, attrs);
        unsigned int nrAttrs = attrs.size();

        /* For each formal argument, get the actual argument.  If
           there is no matching actual argument but the formal
           argument has a default, use the default. */
        unsigned int attrsUsed = 0;
        for (ATermIterator i(formals); i; ++i) {
            Expr name, def;
            DefaultValue def2;
            if (!matchFormal(*i, name, def2)) abort(); /* can't happen */

            Expr value = attrs[name];

            if (value == 0) {
                if (!matchDefaultValue(def2, def)) def = 0;
                if (def == 0) throw TypeError(format("the argument named `%1%' required by the function is missing")
                    % aterm2String(name));
                subsRecursive.set(name, def);
            } else {
                attrsUsed++;
                attrs.remove(name);
                subs.set(name, value);
            }

        }

        /* Check that each actual argument is listed as a formal
           argument (unless the attribute match specifies a `...'). */
        if (ellipsis == eFalse && attrsUsed != nrAttrs)
            throw TypeError(format("the function does not expect an argument named `%1%'")
                % aterm2String(attrs.begin()->key));
    }

    else abort();
}


/* Substitute an argument set into the body of a function. */
static Expr substArgs(EvalState & state,
    Expr body, Pattern pat, Expr arg)
{
    ATermMap subs(16), subsRecursive(16);
    
    patternMatch(state, pat, arg, subs, subsRecursive);

    /* If we used any default values, make a recursive attribute set
       out of the (argument-name, value) tuples.  This is so that we
       can support default values that refer to each other, e.g.  ({x,
       y ? x + x}: y) {x = "foo";} evaluates to "foofoo". */
    if (subsRecursive.size() != 0) {
        ATermList recAttrs = ATempty;
        foreach (ATermMap::const_iterator, i, subs)
            recAttrs = ATinsert(recAttrs, makeBind(i->key, i->value, makeNoPos()));
        foreach (ATermMap::const_iterator, i, subsRecursive)
            recAttrs = ATinsert(recAttrs, makeBind(i->key, i->value, makeNoPos()));
        Expr rec = makeRec(recAttrs, ATempty);
        foreach (ATermMap::const_iterator, i, subsRecursive)
            subs.set(i->key, makeSelect(rec, i->key));
    }

    return substitute(Substitution(0, &subs), body);
}


/* Transform a mutually recursive set into a non-recursive set.  Each
   attribute is transformed into an expression that has all references
   to attributes substituted with selection expressions on the
   original set.  E.g., e = `rec {x = f x y; y = x;}' becomes `{x = f
   (e.x) (e.y); y = e.x;}'. */
LocalNoInline(ATerm expandRec(EvalState & state, ATerm e, ATermList rbnds, ATermList nrbnds))
{
    ATerm name;
    Expr e2;
    Pos pos;
    Expr eOverrides = 0;

    /* Create the substitution list. */
    ATermMap subs(ATgetLength(rbnds) + ATgetLength(nrbnds));
    for (ATermIterator i(rbnds); i; ++i) {
        if (!matchBind(*i, name, e2, pos)) abort(); /* can't happen */
        subs.set(name, makeSelect(e, name));
    }
    for (ATermIterator i(nrbnds); i; ++i) {
        if (!matchBind(*i, name, e2, pos)) abort(); /* can't happen */
        if (name == sOverrides) eOverrides = e2;
        subs.set(name, e2);
    }

    /* If the rec contains an attribute called `__overrides', then
       evaluate it, and add the attributes in that set to the rec.
       This allows overriding of recursive attributes, which is
       otherwise not possible.  (You can use the // operator to
       replace an attribute, but other attributes in the rec will
       still reference the original value, because that value has been
       substituted into the bodies of the other attributes.  Hence we
       need __overrides.) */
    ATermMap overrides;
    if (eOverrides) {
        eOverrides = evalExpr(state, eOverrides);
        queryAllAttrs(eOverrides, overrides, false);
        foreach (ATermMap::const_iterator, i, overrides)
            subs.set(i->key, i->value);
    }

    Substitution subs_(0, &subs);

    /* Create the non-recursive set. */
    ATermMap as(ATgetLength(rbnds) + ATgetLength(nrbnds));
    for (ATermIterator i(rbnds); i; ++i) {
        if (!matchBind(*i, name, e2, pos)) abort(); /* can't happen */
        as.set(name, makeAttrRHS(substitute(subs_, e2), pos));
    }

    if (eOverrides)
        foreach (ATermMap::const_iterator, i, overrides)
            as.set(i->key, makeAttrRHS(i->value, makeNoPos()));

    /* Copy the non-recursive bindings.  !!! inefficient */
    for (ATermIterator i(nrbnds); i; ++i) {
        if (!matchBind(*i, name, e2, pos)) abort(); /* can't happen */
        as.set(name, makeAttrRHS(e2, pos));
    }

    return makeAttrs(as);
}


static void flattenList(EvalState & state, Expr e, ATermList & result)
{
    ATermList es;
    e = evalExpr(state, e);
    if (matchList(e, es))
        for (ATermIterator i(es); i; ++i)
            flattenList(state, *i, result);
    else
        result = ATinsert(result, e);
}


ATermList flattenList(EvalState & state, Expr e)
{
    ATermList result = ATempty;
    flattenList(state, e, result);
    return ATreverse(result);
}


Expr autoCallFunction(Expr e, const ATermMap & args)
{
    Pattern pat;
    ATerm body, pos, name;
    ATermList formals;
    ATermBool ellipsis;
    
    if (matchFunction(e, pat, body, pos) && matchAttrsPat(pat, formals, ellipsis, name)) {
        ATermMap actualArgs(ATgetLength(formals));
        
        for (ATermIterator i(formals); i; ++i) {
            Expr name, def, value; ATerm def2;
            if (!matchFormal(*i, name, def2)) abort();
            if ((value = args.get(name)))
                actualArgs.set(name, makeAttrRHS(value, makeNoPos()));
            else if (!matchDefaultValue(def2, def))
                throw TypeError(format("cannot auto-call a function that has an argument without a default value (`%1%')")
                    % aterm2String(name));
        }
        
        e = makeCall(e, makeAttrs(actualArgs));
    }
    
    return e;
}


/* Evaluation of various language constructs.  These have been taken
   out of evalExpr2 to reduce stack space usage.  (GCC is really dumb
   about stack space: it just adds up all the local variables and
   temporaries of every scope into one huge stack frame.  This is
   really bad for deeply recursive functions.) */


LocalNoInline(Expr evalVar(EvalState & state, ATerm name))
{
    ATerm primOp = state.primOps.get(name);
    if (!primOp)
        throw EvalError(format("impossible: undefined variable `%1%'") % aterm2String(name));
    int arity;
    ATermBlob fun;
    if (!matchPrimOpDef(primOp, arity, fun)) abort();
    if (arity == 0)
        /* !!! backtrace for primop call */
        return ((PrimOp) ATgetBlobData(fun)) (state, ATermVector());
    else
        return makePrimOp(arity, fun, ATempty);
}


LocalNoInline(Expr evalCall(EvalState & state, Expr fun, Expr arg))
{
    Pattern pat;
    ATerm pos;
    Expr body;
        
    /* Evaluate the left-hand side. */
    fun = evalExpr(state, fun);

    /* Is it a primop or a function? */
    int arity;
    ATermBlob funBlob;
    ATermList args;
    if (matchPrimOp(fun, arity, funBlob, args)) {
        args = ATinsert(args, arg);
        if (ATgetLength(args) == arity) {
            /* Put the arguments in a vector in reverse (i.e.,
               actual) order. */
            ATermVector args2(arity);
            for (ATermIterator i(args); i; ++i)
                args2[--arity] = *i;
            /* !!! backtrace for primop call */
            return ((PrimOp) ATgetBlobData(funBlob))
                (state, args2);
        } else
            /* Need more arguments, so propagate the primop. */
            return makePrimOp(arity, funBlob, args);
    }

    else if (matchFunction(fun, pat, body, pos)) {
        try {
            return evalExpr(state, substArgs(state, body, pat, arg));
        } catch (Error & e) {
            addErrorPrefix(e, "while evaluating the function at %1%:\n",
                showPos(pos));
            throw;
        }
    }
        
    else throwTypeError(
        "attempt to call something which is neither a function nor a primop (built-in operation) but %1%",
        showType(fun));
}


LocalNoInline(Expr evalWith(EvalState & state, Expr defs, Expr body, ATerm pos))
{
    ATermMap attrs;
    try {
        defs = evalExpr(state, defs);
        queryAllAttrs(defs, attrs);
    } catch (Error & e) {
        addErrorPrefix(e, "while evaluating the `with' definitions at %1%:\n",
            showPos(pos));
        throw;
    }
    try {
        body = substitute(Substitution(0, &attrs), body);
        checkVarDefs(state.primOps, body);
        return evalExpr(state, body);
    } catch (Error & e) {
        addErrorPrefix(e, "while evaluating the `with' body at %1%:\n",
            showPos(pos));
        throw;
    } 
}


/* Implementation of the `==' and `!=' operators. */
LocalNoInline(bool areEqual(EvalState & state, Expr e1, Expr e2))
{
    e1 = evalExpr(state, e1);
    e2 = evalExpr(state, e2);

    /* We cannot test functions/primops for equality, and we currently
       don't support testing equality between attribute sets or lists
       - that would have to be a deep equality test to be sound. */
    AFun sym1 = ATgetAFun(e1);
    AFun sym2 = ATgetAFun(e2);

    if (sym1 != sym2) return false;

    /* Functions are incomparable. */
    if (sym1 == symFunction || sym1 == symPrimOp) return false;

    if (!state.allowUnsafeEquality && sym1 == symAttrs)
        throw EvalError("comparison of attribute sets is not implemented");

    /* !!! This allows comparisons of infinite data structures to
       succeed, such as `let x = [x]; in x == x'.  This is
       undesirable, since equivalent (?) terms such as `let x = [x]; y
       = [y]; in x == y' don't terminate. */
    if (e1 == e2) return true;
    
    if (sym1 == symList) {
        ATermList es1; matchList(e1, es1);
        ATermList es2; matchList(e2, es2);
        if (ATgetLength(es1) != ATgetLength(es2)) return false;
        ATermIterator i(es1), j(es2);
        while (*i) {
            if (!areEqual(state, *i, *j)) return false;
            ++i; ++j;
        }
        return true;
    }
    
    return false;
}


Expr evalExpr2(EvalState & state, Expr e)
{
    /* When changing this function, make sure that you don't cause a
       (large) increase in stack consumption! */
    
    char x;
    if (&x < deepestStack) deepestStack = &x;
    
    Expr e1, e2, e3;
    ATerm name, pos;
    AFun sym = ATgetAFun(e);

    /* Normal forms. */
    if (sym == symStr ||
        sym == symPath ||
        sym == symNull ||
        sym == symInt ||
        sym == symBool ||
        sym == symFunction ||
        sym == symAttrs ||
        sym == symList ||
        sym == symPrimOp)
        return e;
    
    /* The `Closed' constructor is just a way to prevent substitutions
       into expressions not containing free variables. */
    if (matchClosed(e, e1))
        return evalExpr(state, e1);

    /* Any encountered variables must be primops (since undefined
       variables are detected after parsing). */
    if (matchVar(e, name)) return evalVar(state, name);

    /* Function application. */
    if (matchCall(e, e1, e2)) return evalCall(state, e1, e2);

    /* Attribute selection. */
    if (matchSelect(e, e1, name)) return evalSelect(state, e1, name);

    /* Mutually recursive sets. */
    ATermList rbnds, nrbnds;
    if (matchRec(e, rbnds, nrbnds))
        return expandRec(state, e, rbnds, nrbnds);

    /* Conditionals. */
    if (matchIf(e, e1, e2, e3))
        return evalExpr(state, evalBool(state, e1) ? e2 : e3);

    /* Assertions. */
    if (matchAssert(e, e1, e2, pos)) return evalAssert(state, e1, e2, pos);

    /* Withs. */
    if (matchWith(e, e1, e2, pos)) return evalWith(state, e1, e2, pos);

    /* Generic equality/inequality.  Note that the behaviour on
       composite data (lists, attribute sets) and functions is
       undefined, since the subterms of those terms are not evaluated.
       However, we don't want to make (==) strict, because that would
       make operations like `big_derivation == null' very slow (unless
       we were to evaluate them side-by-side). */
    if (matchOpEq(e, e1, e2)) return makeBool(areEqual(state, e1, e2));
        
    if (matchOpNEq(e, e1, e2)) return makeBool(!areEqual(state, e1, e2));
        
    /* Negation. */
    if (matchOpNot(e, e1))
        return makeBool(!evalBool(state, e1));

    /* Implication. */
    if (matchOpImpl(e, e1, e2))
        return makeBool(!evalBool(state, e1) || evalBool(state, e2));

    /* Conjunction (logical AND). */
    if (matchOpAnd(e, e1, e2))
        return makeBool(evalBool(state, e1) && evalBool(state, e2));

    /* Disjunction (logical OR). */
    if (matchOpOr(e, e1, e2))
        return makeBool(evalBool(state, e1) || evalBool(state, e2));

    /* Attribute set update (//). */
    if (matchOpUpdate(e, e1, e2))
        return updateAttrs(evalExpr(state, e1), evalExpr(state, e2));

    /* Attribute existence test (?). */
    if (matchOpHasAttr(e, e1, name)) return evalHasAttr(state, e1, name);

    /* String or path concatenation. */
    if (sym == symOpPlus || sym == symConcatStrings)
        return evalPlusConcat(state, e);

    /* Backwards compatability: subpath operator (~). */
    if (matchSubPath(e, e1, e2)) return evalSubPath(state, e1, e2);

    /* List concatenation. */
    if (matchOpConcat(e, e1, e2)) return evalOpConcat(state, e1, e2);

    /* Barf. */
    abort();
}


Expr evalExpr(EvalState & state, Expr e)
{
    checkInterrupt();

#if 0
    startNest(nest, lvlVomit,
        format("evaluating expression: %1%") % e);
#endif

    state.nrEvaluated++;

    /* Consult the memo table to quickly get the normal form of
       previously evaluated expressions. */
    Expr nf = state.normalForms.get(e);
    if (nf) {
        if (nf == makeBlackHole())
            throwEvalError("infinite recursion encountered");
        state.nrCached++;
        return nf;
    }

    /* Otherwise, evaluate and memoize. */
    state.normalForms.set(e, makeBlackHole());
    try {
        nf = evalExpr2(state, e);
    } catch (Error & err) {
        state.normalForms.remove(e);
        throw;
    }
    state.normalForms.set(e, nf);
    return nf;
}


static Expr strictEvalExpr(EvalState & state, Expr e, ATermMap & nfs);


static Expr strictEvalExpr_(EvalState & state, Expr e, ATermMap & nfs)
{
    e = evalExpr(state, e);

    ATermList as;
    if (matchAttrs(e, as)) {
        ATermList as2 = ATempty;
        for (ATermIterator i(as); i; ++i) {
            ATerm name; Expr e; ATerm pos;
            if (!matchBind(*i, name, e, pos)) abort(); /* can't happen */
            as2 = ATinsert(as2, makeBind(name, strictEvalExpr(state, e, nfs), pos));
        }
        return makeAttrs(ATreverse(as2));
    }
    
    ATermList es;
    if (matchList(e, es)) {
        ATermList es2 = ATempty;
        for (ATermIterator i(es); i; ++i)
            es2 = ATinsert(es2, strictEvalExpr(state, *i, nfs));
        return makeList(ATreverse(es2));
    }
    
    return e;
}


static Expr strictEvalExpr(EvalState & state, Expr e, ATermMap & nfs)
{
    Expr nf = nfs.get(e);
    if (nf) return nf;

    nf = strictEvalExpr_(state, e, nfs);

    nfs.set(e, nf);
    
    return nf;
}


Expr strictEvalExpr(EvalState & state, Expr e)
{
    ATermMap strictNormalForms;
    return strictEvalExpr(state, e, strictNormalForms);
}
#endif


void EvalState::printStats()
{
    char x;
    bool showStats = getEnv("NIX_SHOW_STATS", "0") != "0";
    printMsg(showStats ? lvlInfo : lvlDebug,
        format("evaluated %1% expressions, used %2% bytes of stack space, allocated %3% values, allocated %4% environments")
        % nrEvaluated
        % (&x - deepestStack)
        % nrValues
        % nrEnvs);
    if (showStats)
        printATermMapStats();
}

 
}
