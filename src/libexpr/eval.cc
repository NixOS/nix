#include "eval.hh"
#include "parser.hh"
#include "hash.hh"
#include "util.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "globals.hh"

#include <cstring>


#define LocalNoInline(f) static f __attribute__((noinline)); f
#define LocalNoInlineNoReturn(f) static f __attribute__((noinline, noreturn)); f


namespace nix {
    

std::ostream & operator << (std::ostream & str, const Value & v)
{
    switch (v.type) {
    case tInt:
        str << v.integer;
        break;
    case tBool:
        str << (v.boolean ? "true" : "false");
        break;
    case tString:
        str << "\"";
        for (const char * i = v.string.s; *i; i++)
            if (*i == '\"' || *i == '\\') str << "\\" << *i;
            else if (*i == '\n') str << "\\n";
            else if (*i == '\r') str << "\\r";
            else if (*i == '\t') str << "\\t";
            else str << *i;
        str << "\"";
        break;
    case tPath:
        str << v.path; // !!! escaping?
        break;
    case tNull:
        str << "true";
        break;
    case tAttrs: {
        str << "{ ";
        typedef std::map<string, Value *> Sorted;
        Sorted sorted;
        foreach (Bindings::iterator, i, *v.attrs)
            sorted[i->first] = &i->second.value;
        foreach (Sorted::iterator, i, sorted)
            str << i->first << " = " << *i->second << "; ";
        str << "}";
        break;
    }
    case tList:
        str << "[ ";
        for (unsigned int n = 0; n < v.list.length; ++n)
            str << *v.list.elems[n] << " ";
        str << "]";
        break;
    case tThunk:
    case tApp:
    case tCopy:
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


string showType(const Value & v)
{
    switch (v.type) {
        case tInt: return "an integer";
        case tBool: return "a boolean";
        case tString: return "a string";
        case tPath: return "a path";
        case tNull: return "null";
        case tAttrs: return "an attribute set";
        case tList: return "a list";
        case tThunk: return "a thunk";
        case tApp: return "a function application";
        case tLambda: return "a function";
        case tCopy: return "a copy";
        case tBlackhole: return "a black hole";
        case tPrimOp: return "a built-in function";
        case tPrimOpApp: return "a partially applied built-in function";
    }
    abort();
}


EvalState::EvalState()
    : sWith(symbols.create("<with>"))
    , sOutPath(symbols.create("outPath"))
    , sDrvPath(symbols.create("drvPath"))
    , sType(symbols.create("type"))
    , sMeta(symbols.create("meta"))
    , sName(symbols.create("name"))
    , sSystem(symbols.create("system"))
    , sOverrides(symbols.create("__overrides"))
    , baseEnv(allocEnv(128))
    , baseEnvDispl(0)
    , staticBaseEnv(false, 0)
{
    nrEnvs = nrValuesInEnvs = nrValues = nrListElems = 0;
    nrEvaluated = recursionDepth = maxRecursionDepth = 0;
    deepestStack = (char *) -1;

    createBaseEnv();
    
    allowUnsafeEquality = getEnv("NIX_NO_UNSAFE_EQ", "") == "";
}


EvalState::~EvalState()
{
    assert(recursionDepth == 0);
}


void EvalState::addConstant(const string & name, Value & v)
{
    staticBaseEnv.vars[symbols.create(name)] = baseEnvDispl;
    baseEnv.values[baseEnvDispl++] = v;
    string name2 = string(name, 0, 2) == "__" ? string(name, 2) : name;
    (*baseEnv.values[0].attrs)[symbols.create(name2)].value = v;
}


void EvalState::addPrimOp(const string & name,
    unsigned int arity, PrimOp primOp)
{
    Value v;
    string name2 = string(name, 0, 2) == "__" ? string(name, 2) : name;
    v.type = tPrimOp;
    v.primOp.arity = arity;
    v.primOp.fun = primOp;
    v.primOp.name = strdup(name2.c_str());
    staticBaseEnv.vars[symbols.create(name)] = baseEnvDispl;
    baseEnv.values[baseEnvDispl++] = v;
    (*baseEnv.values[0].attrs)[symbols.create(name2)].value = v;
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

LocalNoInlineNoReturn(void throwEvalError(const char * s, const string & s2, const string & s3))
{
    throw EvalError(format(s) % s2 % s3);
}

LocalNoInlineNoReturn(void throwTypeError(const char * s))
{
    throw TypeError(s);
}

LocalNoInlineNoReturn(void throwTypeError(const char * s, const string & s2))
{
    throw TypeError(format(s) % s2);
}

LocalNoInlineNoReturn(void throwTypeError(const char * s, const Pos & pos, const string & s2))
{
    throw TypeError(format(s) % pos % s2);
}

LocalNoInlineNoReturn(void throwTypeError(const char * s, const Pos & pos))
{
    throw TypeError(format(s) % pos);
}

LocalNoInlineNoReturn(void throwAssertionError(const char * s, const Pos & pos))
{
    throw AssertionError(format(s) % pos);
}

LocalNoInline(void addErrorPrefix(Error & e, const char * s, const string & s2))
{
    e.addPrefix(format(s) % s2);
}

LocalNoInline(void addErrorPrefix(Error & e, const char * s, const Pos & pos))
{
    e.addPrefix(format(s) % pos);
}

LocalNoInline(void addErrorPrefix(Error & e, const char * s, const string & s2, const Pos & pos))
{
    e.addPrefix(format(s) % s2 % pos);
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
        unsigned int n = 0;
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


Value * EvalState::lookupVar(Env * env, const VarRef & var)
{
    for (unsigned int l = var.level; l; --l, env = env->up) ;
    
    if (var.fromWith) {
        while (1) {
            Bindings::iterator j = env->values[0].attrs->find(var.name);
            if (j != env->values[0].attrs->end())
                return &j->second.value;
            if (env->prevWith == 0)
                throwEvalError("undefined variable `%1%'", var.name);
            for (unsigned int l = env->prevWith; l; --l, env = env->up) ;
        }
    } else
        return &env->values[var.displ];
}


Value * EvalState::allocValues(unsigned int count)
{
    nrValues += count;
    return new Value[count]; // !!! check destructor
}


Env & EvalState::allocEnv(unsigned int size)
{
    nrEnvs++;
    nrValuesInEnvs += size;
    Env * env = (Env *) malloc(sizeof(Env) + size * sizeof(Value));
    return *env;
}


void EvalState::mkList(Value & v, unsigned int length)
{
    v.type = tList;
    v.list.length = length;
    v.list.elems = new Value *[length];
    nrListElems += length;
}


void EvalState::mkAttrs(Value & v)
{
    v.type = tAttrs;
    v.attrs = new Bindings;
}


void EvalState::mkThunk_(Value & v, Expr * expr)
{
    mkThunk(v, baseEnv, expr);
}


void EvalState::cloneAttrs(Value & src, Value & dst)
{
    mkAttrs(dst);
    foreach (Bindings::iterator, i, *src.attrs) {
        Attr & a = (*dst.attrs)[i->first];
        mkCopy(a.value, i->second.value);
        a.pos = i->second.pos;
    }
}


void EvalState::evalFile(const Path & path, Value & v)
{
    startNest(nest, lvlTalkative, format("evaluating file `%1%'") % path);

    Expr * e = parseTrees[path];

    if (!e) {
        e = parseExprFromFile(*this, path);
        parseTrees[path] = e;
    }
    
    try {
        eval(e, v);
    } catch (Error & e) {
        addErrorPrefix(e, "while evaluating the file `%1%':\n", path);
        throw;
    }
}


struct RecursionCounter
{
    EvalState & state;
    RecursionCounter(EvalState & state) : state(state)
    {
        state.recursionDepth++;
        if (state.recursionDepth > state.maxRecursionDepth)
            state.maxRecursionDepth = state.recursionDepth;
    }
    ~RecursionCounter()
    {   
        state.recursionDepth--;
    }
};


void EvalState::eval(Env & env, Expr * e, Value & v)
{
    /* When changing this function, make sure that you don't cause a
       (large) increase in stack consumption! */

    /* !!! Disable this eventually. */
    RecursionCounter r(*this);
    char x;
    if (&x < deepestStack) deepestStack = &x;
    
    //debug(format("eval: %1%") % *e);

    checkInterrupt();

    nrEvaluated++;

    e->eval(*this, env, v);
}


void EvalState::eval(Expr * e, Value & v)
{
    eval(baseEnv, e, v);
}


bool EvalState::evalBool(Env & env, Expr * e)
{
    Value v;
    eval(env, e, v);
    if (v.type != tBool)
        throwTypeError("value is %1% while a Boolean was expected", showType(v));
    return v.boolean;
}


void EvalState::evalAttrs(Env & env, Expr * e, Value & v)
{
    eval(env, e, v);
    if (v.type != tAttrs)
        throwTypeError("value is %1% while an attribute set was expected", showType(v));
}


void Expr::eval(EvalState & state, Env & env, Value & v)
{
    abort();
}


void ExprInt::eval(EvalState & state, Env & env, Value & v)
{
    mkInt(v, n);
}


void ExprString::eval(EvalState & state, Env & env, Value & v)
{
    mkString(v, s.c_str());
}


void ExprPath::eval(EvalState & state, Env & env, Value & v)
{
    mkPath(v, s.c_str());
}


void ExprAttrs::eval(EvalState & state, Env & env, Value & v)
{
    state.mkAttrs(v);

    if (recursive) {
        /* Create a new environment that contains the attributes in
           this `rec'. */
        Env & env2(state.allocEnv(attrs.size() + inherited.size()));
        env2.up = &env;

        unsigned int displ = 0;
        
        /* The recursive attributes are evaluated in the new
           environment. */
        foreach (Attrs::iterator, i, attrs) {
            nix::Attr & a = (*v.attrs)[i->first];
            mkThunk(a.value, env2, i->second.first);
            mkCopy(env2.values[displ++], a.value);
            a.pos = &i->second.second;
        }

        /* The inherited attributes, on the other hand, are
           evaluated in the original environment. */
        foreach (list<Inherited>::iterator, i, inherited) {
            nix::Attr & a = (*v.attrs)[i->first.name];
            Value * v2 = state.lookupVar(&env, i->first);
            mkCopy(a.value, *v2);
            mkCopy(env2.values[displ++], *v2);
            a.pos = &i->second;
        }

        /* If the rec contains an attribute called `__overrides', then
           evaluate it, and add the attributes in that set to the rec.
           This allows overriding of recursive attributes, which is
           otherwise not possible.  (You can use the // operator to
           replace an attribute, but other attributes in the rec will
           still reference the original value, because that value has
           been substituted into the bodies of the other attributes.
           Hence we need __overrides.) */
        Bindings::iterator overrides = v.attrs->find(state.sOverrides);
        if (overrides != v.attrs->end()) {
            state.forceAttrs(overrides->second.value);
            foreach (Bindings::iterator, i, *overrides->second.value.attrs) {
                nix::Attr & a = (*v.attrs)[i->first];
                mkCopy(a.value, i->second.value);
            }
        }
    }

    else {
        foreach (Attrs::iterator, i, attrs) {
            nix::Attr & a = (*v.attrs)[i->first];
            mkThunk(a.value, env, i->second.first);
            a.pos = &i->second.second;
        }

        foreach (list<Inherited>::iterator, i, inherited) {
            nix::Attr & a = (*v.attrs)[i->first.name];
            mkCopy(a.value, *state.lookupVar(&env, i->first));
            a.pos = &i->second;
        }
    }
}


void ExprLet::eval(EvalState & state, Env & env, Value & v)
{
    /* Create a new environment that contains the attributes in this
       `let'. */
    Env & env2(state.allocEnv(attrs->attrs.size() + attrs->inherited.size()));
    env2.up = &env;

    unsigned int displ = 0;

    /* The recursive attributes are evaluated in the new
       environment. */
    foreach (ExprAttrs::Attrs::iterator, i, attrs->attrs)
        mkThunk(env2.values[displ++], env2, i->second.first);

    /* The inherited attributes, on the other hand, are evaluated in
       the original environment. */
    foreach (list<ExprAttrs::Inherited>::iterator, i, attrs->inherited)
        mkCopy(env2.values[displ++], *state.lookupVar(&env, i->first));

    state.eval(env2, body, v);
}


void ExprList::eval(EvalState & state, Env & env, Value & v)
{
    state.mkList(v, elems.size());
    Value * vs = state.allocValues(v.list.length);
    for (unsigned int n = 0; n < v.list.length; ++n) {
        v.list.elems[n] = &vs[n];
        mkThunk(vs[n], env, elems[n]);
    }
}


void ExprVar::eval(EvalState & state, Env & env, Value & v)
{
    Value * v2 = state.lookupVar(&env, info);
    state.forceValue(*v2);
    v = *v2;
}


void ExprSelect::eval(EvalState & state, Env & env, Value & v)
{
    Value v2;
    state.evalAttrs(env, e, v2);
    Bindings::iterator i = v2.attrs->find(name);
    if (i == v2.attrs->end())
        throwEvalError("attribute `%1%' missing", name);
    try {            
        state.forceValue(i->second.value);
    } catch (Error & e) {
        addErrorPrefix(e, "while evaluating the attribute `%1%' at %2%:\n",
            name, *i->second.pos);
        throw;
    }
    v = i->second.value;
}


void ExprOpHasAttr::eval(EvalState & state, Env & env, Value & v)
{
    Value vAttrs;
    state.evalAttrs(env, e, vAttrs);
    mkBool(v, vAttrs.attrs->find(name) != vAttrs.attrs->end());
}


void ExprLambda::eval(EvalState & state, Env & env, Value & v)
{
    v.type = tLambda;
    v.lambda.env = &env;
    v.lambda.fun = this;
}


void ExprApp::eval(EvalState & state, Env & env, Value & v)
{
    Value vFun;
    state.eval(env, e1, vFun);
    Value vArg;
    mkThunk(vArg, env, e2); // !!! should this be on the heap?
    state.callFunction(vFun, vArg, v);
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
            try {
                primOp->primOp.fun(*this, vArgs, v);
            } catch (Error & e) {
                addErrorPrefix(e, "while evaluating the builtin function `%1%':\n", primOp->primOp.name);
                throw;
            }
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

    unsigned int size =
        (fun.lambda.fun->arg.empty() ? 0 : 1) +
        (fun.lambda.fun->matchAttrs ? fun.lambda.fun->formals->formals.size() : 0);
    Env & env2(allocEnv(size));
    env2.up = fun.lambda.env;

    unsigned int displ = 0;

    if (!fun.lambda.fun->matchAttrs)
        env2.values[displ++] = arg;

    else {
        forceAttrs(arg);
        
        if (!fun.lambda.fun->arg.empty())
            env2.values[displ++] = arg;

        /* For each formal argument, get the actual argument.  If
           there is no matching actual argument but the formal
           argument has a default, use the default. */
        unsigned int attrsUsed = 0;
        foreach (Formals::Formals_::iterator, i, fun.lambda.fun->formals->formals) {
            Bindings::iterator j = arg.attrs->find(i->name);
            if (j == arg.attrs->end()) {
                if (!i->def) throwTypeError("function at %1% called without required argument `%2%'",
                    fun.lambda.fun->pos, i->name);   
                mkThunk(env2.values[displ++], env2, i->def);
            } else {
                attrsUsed++;
                mkCopy(env2.values[displ++], j->second.value);
            }
        }

        /* Check that each actual argument is listed as a formal
           argument (unless the attribute match specifies a `...').
           TODO: show the names of the expected/unexpected
           arguments. */
        if (!fun.lambda.fun->formals->ellipsis && attrsUsed != arg.attrs->size())
            throwTypeError("function at %1% called with unexpected argument", fun.lambda.fun->pos);
    }

    try {
        eval(env2, fun.lambda.fun->body, v);
    } catch (Error & e) {
        addErrorPrefix(e, "while evaluating the function at %1%:\n", fun.lambda.fun->pos);
        throw;
    }
}


void EvalState::autoCallFunction(const Bindings & args, Value & fun, Value & res)
{
    forceValue(fun);

    if (fun.type != tLambda || !fun.lambda.fun->matchAttrs) {
        res = fun;
        return;
    }

    Value actualArgs;
    mkAttrs(actualArgs);

    foreach (Formals::Formals_::iterator, i, fun.lambda.fun->formals->formals) {
        Bindings::const_iterator j = args.find(i->name);
        if (j != args.end())
            (*actualArgs.attrs)[i->name] = j->second;
        else if (!i->def)
            throwTypeError("cannot auto-call a function that has an argument without a default value (`%1%')", i->name);
    }

    callFunction(fun, actualArgs, res);
}


void ExprWith::eval(EvalState & state, Env & env, Value & v)
{
    Env & env2(state.allocEnv(1));
    env2.up = &env;
    env2.prevWith = prevWith;

    state.evalAttrs(env, attrs, env2.values[0]);

    state.eval(env2, body, v);
}


void ExprIf::eval(EvalState & state, Env & env, Value & v)
{
    state.eval(env, state.evalBool(env, cond) ? then : else_, v);
}

    
void ExprAssert::eval(EvalState & state, Env & env, Value & v)
{
    if (!state.evalBool(env, cond))
        throwAssertionError("assertion failed at %1%", pos);
    state.eval(env, body, v);
}

    
void ExprOpNot::eval(EvalState & state, Env & env, Value & v)
{
    mkBool(v, !state.evalBool(env, e));
}


void ExprOpEq::eval(EvalState & state, Env & env, Value & v)
{
    Value v1; state.eval(env, e1, v1);
    Value v2; state.eval(env, e2, v2);
    mkBool(v, state.eqValues(v1, v2));
}


void ExprOpNEq::eval(EvalState & state, Env & env, Value & v)
{
    Value v1; state.eval(env, e1, v1);
    Value v2; state.eval(env, e2, v2);
    mkBool(v, !state.eqValues(v1, v2));
}


void ExprOpAnd::eval(EvalState & state, Env & env, Value & v)
{
    mkBool(v, state.evalBool(env, e1) && state.evalBool(env, e2));
}


void ExprOpOr::eval(EvalState & state, Env & env, Value & v)
{
    mkBool(v, state.evalBool(env, e1) || state.evalBool(env, e2));
}


void ExprOpImpl::eval(EvalState & state, Env & env, Value & v)
{
    mkBool(v, !state.evalBool(env, e1) || state.evalBool(env, e2));
}


void ExprOpUpdate::eval(EvalState & state, Env & env, Value & v)
{
    Value v1, v2;
    state.evalAttrs(env, e1, v1);
    state.evalAttrs(env, e2, v2);

    if (v1.attrs->size() == 0) { v = v2; return; }
    if (v2.attrs->size() == 0) { v = v1; return; }

    state.cloneAttrs(v1, v);

    foreach (Bindings::iterator, i, *v2.attrs) {
        Attr & a = (*v.attrs)[i->first];
        mkCopy(a.value, i->second.value);
        a.pos = i->second.pos;
    }
}


void ExprOpConcatLists::eval(EvalState & state, Env & env, Value & v)
{
    Value v1; state.eval(env, e1, v1);
    state.forceList(v1);
    Value v2; state.eval(env, e2, v2);
    state.forceList(v2);
    state.mkList(v, v1.list.length + v2.list.length);
    for (unsigned int n = 0; n < v1.list.length; ++n)
        v.list.elems[n] = v1.list.elems[n];
    for (unsigned int n = 0; n < v2.list.length; ++n)
        v.list.elems[n + v1.list.length] = v2.list.elems[n];
}


void ExprConcatStrings::eval(EvalState & state, Env & env, Value & v)
{
    PathSet context;
    std::ostringstream s;
        
    bool first = true, isPath = false;
    Value vStr;

    foreach (vector<Expr *>::iterator, i, *es) {
        state.eval(env, *i, vStr);

        /* If the first element is a path, then the result will also
           be a path, we don't copy anything (yet - that's done later,
           since paths are copied when they are used in a derivation),
           and none of the strings are allowed to have contexts. */
        if (first) {
            isPath = vStr.type == tPath;
            first = false;
        }
            
        s << state.coerceToString(vStr, context, false, !isPath);
    }
        
    if (isPath && !context.empty())
        throwEvalError("a string that refers to a store path cannot be appended to a path, in `%1%'", s.str());

    if (isPath)
        mkPath(v, s.str().c_str());
    else
        mkString(v, s.str(), context);
}


void EvalState::forceValue(Value & v)
{
    if (v.type == tThunk) {
        ValueType saved = v.type;
        try {
            v.type = tBlackhole;
            eval(*v.thunk.env, v.thunk.expr, v);
        } catch (Error & e) {
            v.type = saved;
            throw;
        }
    }
    else if (v.type == tCopy) {
        forceValue(*v.val);
        v = *v.val;
    }
    else if (v.type == tApp)
        callFunction(*v.app.left, *v.app.right, v);
    else if (v.type == tBlackhole)
        throwEvalError("infinite recursion encountered");
}


void EvalState::strictForceValue(Value & v)
{
    forceValue(v);
    
    if (v.type == tAttrs) {
        foreach (Bindings::iterator, i, *v.attrs)
            strictForceValue(i->second.value);
    }
    
    else if (v.type == tList) {
        for (unsigned int n = 0; n < v.list.length; ++n)
            strictForceValue(*v.list.elems[n]);
    }
}


int EvalState::forceInt(Value & v)
{
    forceValue(v);
    if (v.type != tInt)
        throwTypeError("value is %1% while an integer was expected", showType(v));
    return v.integer;
}


bool EvalState::forceBool(Value & v)
{
    forceValue(v);
    if (v.type != tBool)
        throwTypeError("value is %1% while a Boolean was expected", showType(v));
    return v.boolean;
}


void EvalState::forceAttrs(Value & v)
{
    forceValue(v);
    if (v.type != tAttrs)
        throwTypeError("value is %1% while an attribute set was expected", showType(v));
}


void EvalState::forceList(Value & v)
{
    forceValue(v);
    if (v.type != tList)
        throwTypeError("value is %1% while a list was expected", showType(v));
}


void EvalState::forceFunction(Value & v)
{
    forceValue(v);
    if (v.type != tLambda && v.type != tPrimOp && v.type != tPrimOpApp)
        throwTypeError("value is %1% while a function was expected", showType(v));
}


string EvalState::forceString(Value & v)
{
    forceValue(v);
    if (v.type != tString)
        throwTypeError("value is %1% while a string was expected", showType(v));
    return string(v.string.s);
}


void copyContext(const Value & v, PathSet & context)
{
    if (v.string.context)
        for (const char * * p = v.string.context; *p; ++p) 
            context.insert(*p);
}


string EvalState::forceString(Value & v, PathSet & context)
{
    string s = forceString(v);
    copyContext(v, context);
    return s;
}


string EvalState::forceStringNoCtx(Value & v)
{
    string s = forceString(v);
    if (v.string.context)
        throwEvalError("the string `%1%' is not allowed to refer to a store path (such as `%2%')",
            v.string.s, v.string.context[0]);
    return s;
}


bool EvalState::isDerivation(Value & v)
{
    if (v.type != tAttrs) return false;
    Bindings::iterator i = v.attrs->find(sType);
    return i != v.attrs->end() && forceStringNoCtx(i->second.value) == "derivation";
}


string EvalState::coerceToString(Value & v, PathSet & context,
    bool coerceMore, bool copyToStore)
{
    forceValue(v);

    string s;

    if (v.type == tString) {
        copyContext(v, context);
        return v.string.s;
    }

    if (v.type == tPath) {
        Path path(canonPath(v.path));

        if (!copyToStore) return path;
        
        if (nix::isDerivation(path))
            throwEvalError("file names are not allowed to end in `%1%'", drvExtension);

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
        Bindings::iterator i = v.attrs->find(sOutPath);
        if (i == v.attrs->end())
            throwTypeError("cannot coerce an attribute set (except a derivation) to a string");
        return coerceToString(i->second.value, context, coerceMore, copyToStore);
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
                result += coerceToString(*v.list.elems[n],
                    context, coerceMore, copyToStore);
                if (n < v.list.length - 1
                    /* !!! not quite correct */
                    && (v.list.elems[n]->type != tList || v.list.elems[n]->list.length != 0))
                    result += " ";
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
        throwEvalError("string `%1%' doesn't represent an absolute path", path);
    return path;
}


bool EvalState::eqValues(Value & v1, Value & v2)
{
    forceValue(v1);
    forceValue(v2);

    /* !!! Hack to support some old broken code that relies on pointer
       equality tests between attribute sets.  (Specifically,
       builderDefs calls uniqList on a list of attribute sets.)  Will
       remove this eventually. */
    if (&v1 == &v2) return true;

    if (v1.type != v2.type) return false;

    switch (v1.type) {

        case tInt:
            return v1.integer == v2.integer;

        case tBool:
            return v1.boolean == v2.boolean;

        case tString: {
            /* Compare both the string and its context. */
            if (strcmp(v1.string.s, v2.string.s) != 0) return false;
            const char * * p = v1.string.context, * * q = v2.string.context;
            if (!p && !q) return true;
            if (!p || !q) return false;
            for ( ; *p && *q; ++p, ++q)
                if (strcmp(*p, *q) != 0) return false;
            if (*p || *q) return false;
            return true;
        }

        case tPath:
            return strcmp(v1.path, v2.path) == 0;

        case tNull:
            return true;

        case tList:
            if (v1.list.length != v2.list.length) return false;
            for (unsigned int n = 0; n < v1.list.length; ++n)
                if (!eqValues(*v1.list.elems[n], *v2.list.elems[n])) return false;
            return true;

        case tAttrs: {
            if (v1.attrs->size() != v2.attrs->size()) return false;
            Bindings::iterator i, j;
            for (i = v1.attrs->begin(), j = v2.attrs->begin(); i != v1.attrs->end(); ++i, ++j)
                if (i->first != j->first || !eqValues(i->second.value, j->second.value))
                    return false;
            return true;
        }

        /* Functions are incomparable. */
        case tLambda:
        case tPrimOp:
        case tPrimOpApp:
            return false;

        default:
            throwEvalError("cannot compare %1% with %2%", showType(v1), showType(v2));
    }
}


void EvalState::printStats()
{
    char x;
    bool showStats = getEnv("NIX_SHOW_STATS", "0") != "0";
    Verbosity v = showStats ? lvlInfo : lvlDebug;
    printMsg(v, "evaluation statistics:");
    printMsg(v, format("  expressions evaluated: %1%") % nrEvaluated);
    printMsg(v, format("  stack space used: %1% bytes") % (&x - deepestStack));
    printMsg(v, format("  max eval() nesting depth: %1%") % maxRecursionDepth);
    printMsg(v, format("  stack space per eval() level: %1% bytes")
        % ((&x - deepestStack) / (float) maxRecursionDepth));
    printMsg(v, format("  environments allocated: %1% (%2% bytes)")
        % nrEnvs % (nrEnvs * sizeof(Env)));
    printMsg(v, format("  values allocated in environments: %1% (%2% bytes)")
        % nrValuesInEnvs % (nrValuesInEnvs * sizeof(Value)));
    printMsg(v, format("  list elements: %1% (%2% bytes)")
        % nrListElems % (nrListElems * sizeof(Value *)));
    printMsg(v, format("  misc. values allocated: %1% (%2% bytes)")
        % nrValues % (nrValues * sizeof(Value)));
    printMsg(v, format("  symbols in symbol table: %1%") % symbols.size());
}


}
