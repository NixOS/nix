#include "eval.hh"
#include "parser.hh"
#include "hash.hh"
#include "util.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "nixexpr-ast.hh"
#include "globals.hh"


#define LocalNoInline(f) static f __attribute__((noinline)); f
#define LocalNoInlineNoReturn(f) static f __attribute__((noinline, noreturn)); f


namespace nix {
    

EvalState::EvalState()
    : normalForms(32768), primOps(128)
{
    nrEvaluated = nrCached = 0;

    initNixExprHelpers();

    addPrimOps();
}


void EvalState::addPrimOp(const string & name,
    unsigned int arity, PrimOp primOp)
{
    primOps.set(toATerm(name), makePrimOpDef(arity, ATmakeBlob(0, (void *) primOp)));
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


/* Pattern-match `pat' against `arg'.  The result is a set of
   substitutions (`subs') and a set of recursive substitutions
   (`subsRecursive').  The latter can refer to the variables bound by
   both `subs' and `subsRecursive'. */
static void patternMatch(EvalState & state,
    Pattern pat, Expr arg, ATermMap & subs, ATermMap & subsRecursive)
{
    ATerm name;
    ATermList formals;
    Pattern pat1, pat2;
    ATermBool ellipsis;
    
    if (matchVarPat(pat, name)) 
        subs.set(name, arg);

    else if (matchAttrsPat(pat, formals, ellipsis)) {

        arg = evalExpr(state, arg);

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

    else if (matchAtPat(pat, pat1, pat2)) {
        patternMatch(state, pat1, arg, subs, subsRecursive);
        patternMatch(state, pat2, arg, subs, subsRecursive);
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
        if (name == sOverrides) eOverrides = e2;
        subs.set(name, makeSelect(e, name));
    }
    for (ATermIterator i(nrbnds); i; ++i) {
        if (!matchBind(*i, name, e2, pos)) abort(); /* can't happen */
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


LocalNoInline(Expr updateAttrs(Expr e1, Expr e2))
{
    /* Note: e1 and e2 should be in normal form. */

    ATermMap attrs;
    queryAllAttrs(e1, attrs, true);
    queryAllAttrs(e2, attrs, true);

    return makeAttrs(attrs);
}


string evalString(EvalState & state, Expr e, PathSet & context)
{
    e = evalExpr(state, e);
    string s;
    if (!matchStr(e, s, context))
        throwTypeError("value is %1% while a string was expected", showType(e));
    return s;
}


string evalStringNoCtx(EvalState & state, Expr e)
{
    PathSet context;
    string s = evalString(state, e, context);
    if (!context.empty())
        throw EvalError(format("the string `%1%' is not allowed to refer to a store path (such as `%2%')")
            % s % *(context.begin()));
    return s;
}


int evalInt(EvalState & state, Expr e)
{
    e = evalExpr(state, e);
    int i;
    if (!matchInt(e, i))
        throwTypeError("value is %1% while an integer was expected", showType(e));
    return i;
}


bool evalBool(EvalState & state, Expr e)
{
    e = evalExpr(state, e);
    if (e == eTrue) return true;
    else if (e == eFalse) return false;
    else throwTypeError("value is %1% while a boolean was expected", showType(e));
}


ATermList evalList(EvalState & state, Expr e)
{
    e = evalExpr(state, e);
    ATermList list;
    if (!matchList(e, list))
        throwTypeError("value is %1% while a list was expected", showType(e));
    return list;
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


string coerceToString(EvalState & state, Expr e, PathSet & context,
    bool coerceMore, bool copyToStore)
{
    e = evalExpr(state, e);

    string s;

    if (matchStr(e, s, context)) return s;

    ATerm s2;
    if (matchPath(e, s2)) {
        Path path(canonPath(aterm2String(s2)));

        if (!copyToStore) return path;
        
        if (isDerivation(path))
            throw EvalError(format("file names are not allowed to end in `%1%'")
                % drvExtension);

        Path dstPath;
        if (state.srcToStore[path] != "")
            dstPath = state.srcToStore[path];
        else {
            dstPath = readOnlyMode
                ? computeStorePathForPath(path).first
                : store->addToStore(path);
            state.srcToStore[path] = dstPath;
            printMsg(lvlChatty, format("copied source `%1%' -> `%2%'")
                % path % dstPath);
        }

        context.insert(dstPath);
        return dstPath;
    }
        
    ATermList es;
    if (matchAttrs(e, es)) {
        Expr e2 = queryAttr(e, "outPath");
        if (!e2) throwTypeError("cannot coerce an attribute set (except a derivation) to a string");
        return coerceToString(state, e2, context, coerceMore, copyToStore);
    }

    if (coerceMore) {

        /* Note that `false' is represented as an empty string for
           shell scripting convenience, just like `null'. */
        if (e == eTrue) return "1";
        if (e == eFalse) return "";
        int n;
        if (matchInt(e, n)) return int2String(n);
        if (matchNull(e)) return "";

        if (matchList(e, es)) {
            string result;
            es = flattenList(state, e);
            bool first = true;
            for (ATermIterator i(es); i; ++i) {
                if (!first) result += " "; else first = false;
                result += coerceToString(state, *i,
                    context, coerceMore, copyToStore);
            }
            return result;
        }
    }
    
    throwTypeError("cannot coerce %1% to a string", showType(e));
}


/* Common implementation of `+', ConcatStrings and `~'. */
static ATerm concatStrings(EvalState & state, ATermVector & args,
    string separator = "")
{
    if (args.empty()) return makeStr("", PathSet());
    
    PathSet context;
    std::ostringstream s;

    /* If the first element is a path, then the result will also be a
       path, we don't copy anything (yet - that's done later, since
       paths are copied when they are used in a derivation), and none
       of the strings are allowed to have contexts. */
    ATerm dummy;
    args.front() = evalExpr(state, args.front());
    bool isPath = matchPath(args.front(), dummy);

    for (ATermVector::const_iterator i = args.begin(); i != args.end(); ++i) {
        if (i != args.begin()) s << separator;
        s << coerceToString(state, *i, context, false, !isPath);
    }

    if (isPath && !context.empty())
        throw EvalError(format("a string that refers to a store path cannot be appended to a path, in `%1%'")
            % s.str());
    
    return isPath
        ? makePath(toATerm(s.str()))
        : makeStr(s.str(), context);
}


Path coerceToPath(EvalState & state, Expr e, PathSet & context)
{
    string path = coerceToString(state, e, context, false, false);
    if (path == "" || path[0] != '/')
        throw EvalError(format("string `%1%' doesn't represent an absolute path") % path);
    return path;
}


Expr autoCallFunction(Expr e, const ATermMap & args)
{
    Pattern pat;
    ATerm body, pos;
    ATermList formals;
    ATermBool ellipsis;
    
    /* !!! this should be more general */
    if (matchFunction(e, pat, body, pos) && matchAttrsPat(pat, formals, ellipsis)) {
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


LocalNoInline(Expr evalSelect(EvalState & state, Expr e, ATerm name))
{
    ATerm pos;
    string s = aterm2String(name);
    Expr a = queryAttr(evalExpr(state, e), s, pos);
    if (!a) throwEvalError("attribute `%1%' missing", s);
    try {
        return evalExpr(state, a);
    } catch (Error & e) {
        addErrorPrefix(e, "while evaluating the attribute `%1%' at %2%:\n",
            s, showPos(pos));
        throw;
    }
}


LocalNoInline(Expr evalAssert(EvalState & state, Expr cond, Expr body, ATerm pos))
{
    if (!evalBool(state, cond))
        throw AssertionError(format("assertion failed at %1%") % showPos(pos));
    return evalExpr(state, body);
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


LocalNoInline(Expr evalHasAttr(EvalState & state, Expr e, ATerm name))
{
    ATermMap attrs;
    queryAllAttrs(evalExpr(state, e), attrs);
    return makeBool(attrs.get(name) != 0);
}


LocalNoInline(Expr evalPlusConcat(EvalState & state, Expr e))
{
    Expr e1, e2;
    ATermList es;
    
    ATermVector args;
    
    if (matchOpPlus(e, e1, e2)) {

        /* !!! Awful compatibility hack for `drv + /path'.
           According to regular concatenation, /path should be
           copied to the store and its store path should be
           appended to the string.  However, in Nix <= 0.10, /path
           was concatenated.  So handle that case separately, but
           do print out a warning.  This code can go in Nix 0.12,
           maybe. */
        e1 = evalExpr(state, e1);
        e2 = evalExpr(state, e2);

        ATermList as;
        ATerm p;
        if (matchAttrs(e1, as) && matchPath(e2, p)) {
            static bool haveWarned = false;
            warnOnce(haveWarned, format(
                    "concatenation of a derivation and a path is deprecated; "
                    "you should write `drv + \"%1%\"' instead of `drv + %1%'")
                % aterm2String(p));
            PathSet context;
            return makeStr(
                coerceToString(state, makeSelect(e1, toATerm("outPath")), context)
                + aterm2String(p), context);
        }

        args.push_back(e1);
        args.push_back(e2);
    }

    else if (matchConcatStrings(e, es))
        for (ATermIterator i(es); i; ++i) args.push_back(*i);
        
    try {
        return concatStrings(state, args);
    } catch (Error & e) {
        addErrorPrefix(e, "in a string concatenation:\n");
        throw;
    }
}


LocalNoInline(Expr evalSubPath(EvalState & state, Expr e1, Expr e2))
{
    static bool haveWarned = false;
    warnOnce(haveWarned, "the subpath operator (~) is deprecated, use string concatenation (+) instead");
    ATermVector args;
    args.push_back(e1);
    args.push_back(e2);
    return concatStrings(state, args, "/");
}


LocalNoInline(Expr evalOpConcat(EvalState & state, Expr e1, Expr e2))
{
    try {
        ATermList l1 = evalList(state, e1);
        ATermList l2 = evalList(state, e2);
        return makeList(ATconcat(l1, l2));
    } catch (Error & e) {
        addErrorPrefix(e, "in a list concatenation:\n");
        throw;
    }
}


static char * deepestStack = (char *) -1; /* for measuring stack usage */


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
    if (matchOpEq(e, e1, e2))
        return makeBool(evalExpr(state, e1) == evalExpr(state, e2));

    if (matchOpNEq(e, e1, e2))
        return makeBool(evalExpr(state, e1) != evalExpr(state, e2));

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


Expr evalFile(EvalState & state, const Path & path)
{
    startNest(nest, lvlTalkative, format("evaluating file `%1%'") % path);
    Expr e = parseExprFromFile(state, path);
    try {
        return evalExpr(state, e);
    } catch (Error & e) {
        e.addPrefix(format("while evaluating the file `%1%':\n")
            % path);
        throw;
    }
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


/* Yes, this is a really bad idea... */
extern "C" {
    unsigned long AT_calcAllocatedSize();
}

void printEvalStats(EvalState & state)
{
    char x;
    bool showStats = getEnv("NIX_SHOW_STATS", "0") != "0";
    printMsg(showStats ? lvlInfo : lvlDebug,
        format("evaluated %1% expressions, %2% cache hits, %3%%% efficiency, used %4% ATerm bytes, used %5% bytes of stack space")
        % state.nrEvaluated % state.nrCached
        % ((float) state.nrCached / (float) state.nrEvaluated * 100)
        % AT_calcAllocatedSize()
        % (&x - deepestStack));
    if (showStats)
        printATermMapStats();
}

 
}
