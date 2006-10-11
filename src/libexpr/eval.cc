#include "eval.hh"
#include "parser.hh"
#include "hash.hh"
#include "util.hh"
#include "store.hh"
#include "nixexpr-ast.hh"


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


/* Substitute an argument set into the body of a function. */
static Expr substArgs(EvalState & state,
    Expr body, ATermList formals, Expr arg)
{
    unsigned int nrFormals = ATgetLength(formals);
    ATermMap subs(nrFormals);

    /* Get the actual arguments and put them in the substitution. */
    ATermMap args(128); /* !!! fix */
    queryAllAttrs(arg, args);
    for (ATermMap::const_iterator i = args.begin(); i != args.end(); ++i)
        subs.set(i->key, i->value);
    
    /* Get the formal arguments. */
    ATermVector defsUsed;
    ATermList recAttrs = ATempty;
    for (ATermIterator i(formals); i; ++i) {
        Expr name, def;
        ValidValues valids2;
        DefaultValue def2;
        if (!matchFormal(*i, name, valids2, def2)) abort(); /* can't happen */

        Expr value = subs[name];
        
        if (value == 0) {
            if (!matchDefaultValue(def2, def)) def = 0;
            if (def == 0) throw TypeError(format("the argument named `%1%' required by the function is missing")
                % aterm2String(name));
            value = def;
            defsUsed.push_back(name);
            recAttrs = ATinsert(recAttrs, makeBind(name, def, makeNoPos()));
        }

        ATermList valids;
        if (matchValidValues(valids2, valids)) {
            value = evalExpr(state, value);
            bool found = false;
            for (ATermIterator j(valids); j; ++j) {
                Expr v = evalExpr(state, *j);
                if (value == v) {
                    found = true;
                    break;
                }
            }
            if (!found) throw TypeError(format("the argument named `%1%' has an illegal value")
                % aterm2String(name));            
        }
    }

    /* Make a recursive attribute set out of the (argument-name,
       value) tuples.  This is so that we can support default
       parameters that refer to each other, e.g.  ({x, y ? x + x}: y)
       {x = "foo";} evaluates to "foofoo". */
    if (defsUsed.size() != 0) {
        for (ATermMap::const_iterator i = args.begin(); i != args.end(); ++i)
            recAttrs = ATinsert(recAttrs, makeBind(i->key, i->value, makeNoPos()));
        Expr rec = makeRec(recAttrs, ATempty);
        for (ATermVector::iterator i = defsUsed.begin(); i != defsUsed.end(); ++i)
            subs.set(*i, makeSelect(rec, *i));
    }
    
    if (subs.size() != nrFormals) {
        /* One or more actual arguments were not declared as formal
           arguments.  Find out which. */
        for (ATermIterator i(formals); i; ++i) {
            Expr name; ATerm d1, d2;
            if (!matchFormal(*i, name, d1, d2)) abort();
            subs.remove(name);
        }
        throw TypeError(format("the function does not expect an argument named `%1%'")
            % aterm2String(subs.begin()->key));
    }

    return substitute(Substitution(0, &subs), body);
}


/* Transform a mutually recursive set into a non-recursive set.  Each
   attribute is transformed into an expression that has all references
   to attributes substituted with selection expressions on the
   original set.  E.g., e = `rec {x = f x y; y = x;}' becomes `{x = f
   (e.x) (e.y); y = e.x;}'. */
ATerm expandRec(ATerm e, ATermList rbnds, ATermList nrbnds)
{
    ATerm name;
    Expr e2;
    Pos pos;

    /* Create the substitution list. */
    ATermMap subs(ATgetLength(rbnds) + ATgetLength(nrbnds));
    for (ATermIterator i(rbnds); i; ++i) {
        if (!matchBind(*i, name, e2, pos)) abort(); /* can't happen */
        subs.set(name, makeSelect(e, name));
    }
    for (ATermIterator i(nrbnds); i; ++i) {
        if (!matchBind(*i, name, e2, pos)) abort(); /* can't happen */
        subs.set(name, e2);
    }

    Substitution subs_(0, &subs);

    /* Create the non-recursive set. */
    ATermMap as(ATgetLength(rbnds) + ATgetLength(nrbnds));
    for (ATermIterator i(rbnds); i; ++i) {
        if (!matchBind(*i, name, e2, pos)) abort(); /* can't happen */
        as.set(name, makeAttrRHS(substitute(subs_, e2), pos));
    }

    /* Copy the non-recursive bindings.  !!! inefficient */
    for (ATermIterator i(nrbnds); i; ++i) {
        if (!matchBind(*i, name, e2, pos)) abort(); /* can't happen */
        as.set(name, makeAttrRHS(e2, pos));
    }

    return makeAttrs(as);
}


static Expr updateAttrs(Expr e1, Expr e2)
{
    /* Note: e1 and e2 should be in normal form. */

    ATermMap attrs(128); /* !!! */
    queryAllAttrs(e1, attrs, true);
    queryAllAttrs(e2, attrs, true);

    return makeAttrs(attrs);
}


string evalString(EvalState & state, Expr e)
{
    e = evalExpr(state, e);
    ATerm s;
    if (!matchStr(e, s))
        throw TypeError(format("value is %1% while a string was expected") % showType(e));
    return aterm2String(s);
}


Path evalPath(EvalState & state, Expr e)
{
    e = evalExpr(state, e);
    ATerm s;
    if (!matchPath(e, s))
        throw TypeError(format("value is %1% while a path was expected") % showType(e));
    return aterm2String(s);
}


int evalInt(EvalState & state, Expr e)
{
    e = evalExpr(state, e);
    int i;
    if (!matchInt(e, i))
        throw TypeError(format("value is %1% while an integer was expected") % showType(e));
    return i;
}


bool evalBool(EvalState & state, Expr e)
{
    e = evalExpr(state, e);
    if (e == eTrue) return true;
    else if (e == eFalse) return false;
    else throw TypeError(format("value is %1% while a boolean was expected") % showType(e));
}


ATermList evalList(EvalState & state, Expr e)
{
    e = evalExpr(state, e);
    ATermList list;
    if (!matchList(e, list))
        throw TypeError(format("value is %1% while a list was expected") % showType(e));
    return list;
}


/* String concatenation and context nodes: in order to allow users to
   write things like

     "--with-freetype2-library=" + freetype + "/lib"

   where `freetype' is a derivation, we automatically coerce
   derivations into their output path (e.g.,
   /nix/store/hashcode-freetype) in concatenations.  However, if we do
   this naively, we could introduce an undeclared dependency: when the
   string is used in another derivation, that derivation would not
   have an explicitly dependency on `freetype' in its inputDrvs
   field.  Thus `freetype' would not necessarily be built.

   To prevent this, we wrap the string resulting from the
   concatenation in a *context node*, like this:

     Context([freetype],
       Str("--with-freetype2-library=/nix/store/hashcode-freetype/lib"))

   Thus the context is the list of all derivations used in the
   computation of a value.  These contexts are propagated through
   further concatenations.  In processBinding() in primops.cc, context
   nodes are unwrapped and added to inputDrvs.

   !!! Should the ordering of the context list have a canonical form?

   !!! Contexts are not currently recognised in most places in the
   evaluator. */


/* Coerce a value to a string, keeping track of contexts. */
string coerceToStringWithContext(EvalState & state,
    ATermList & context, Expr e, bool & isPath)
{
    isPath = false;
    
    e = evalExpr(state, e);

    bool isWrapped = false;
    ATermList es;
    ATerm e2;
    if (matchContext(e, es, e2)) {
        isWrapped = true;
        e = e2;
        context = ATconcat(es, context);
    }
    
    ATerm s;
    if (matchStr(e, s)) return aterm2String(s);
    
    if (matchPath(e, s)) {
        isPath = true;
        Path path = aterm2String(s);
        if (isInStore(path) && !isWrapped) {
            context = ATinsert(context, makePath(toATerm(toStorePath(path))));
        }
        return path;
    }

    if (matchAttrs(e, es)) {
        ATermMap attrs(128); /* !!! */
        queryAllAttrs(e, attrs, false);

        Expr a = attrs.get(toATerm("type"));
        if (a && evalString(state, a) == "derivation") {
            a = attrs.get(toATerm("outPath"));
            if (!a) throw TypeError("output path missing from derivation");
            isPath = true;
            context = ATinsert(context, e);
            return evalPath(state, a);
        }
    }
    
    throw TypeError(format("cannot coerce %1% to a string") % showType(e));
}


/* Wrap an expression in a context if the context is not empty. */
Expr wrapInContext(ATermList context, Expr e)
{
    return context == ATempty ? e : makeContext(context, e);
}


static ATerm concatStrings(EvalState & state, const ATermVector & args)
{
    ATermList context = ATempty;
    std::ostringstream s;
    bool isPath = false;

    /* Note that if the first argument in the concatenation is a path,
       then the result is also a path. */

    for (ATermVector::const_iterator i = args.begin(); i != args.end(); ++i) {
        bool isPath2;
        s << coerceToStringWithContext(state, context, *i, isPath2);
        if (i == args.begin()) isPath = isPath2;
    }

    return wrapInContext(context, isPath
        ? makePath(toATerm(canonPath(s.str())))
        : makeStr(toATerm(s.str())));
}


Expr autoCallFunction(Expr e, const ATermMap & args)
{
    ATermList formals;
    ATerm body, pos;
    
    if (matchFunction(e, formals, body, pos)) {
        ATermMap actualArgs(128);
        
        for (ATermIterator i(formals); i; ++i) {
            Expr name, def, value; ATerm values, def2;
            if (!matchFormal(*i, name, values, def2)) abort();
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


Expr evalExpr2(EvalState & state, Expr e)
{
    Expr e1, e2, e3, e4;
    ATerm name, pos;
    AFun sym = ATgetAFun(e);

    /* Normal forms. */
    if (sym == symStr ||
        sym == symPath ||
        sym == symNull ||
        sym == symInt ||
        sym == symBool ||
        sym == symFunction ||
        sym == symFunction1 ||
        sym == symAttrs ||
        sym == symList ||
        sym == symPrimOp ||
        sym == symContext)
        return e;
    
    /* The `Closed' constructor is just a way to prevent substitutions
       into expressions not containing free variables. */
    if (matchClosed(e, e1))
        return evalExpr(state, e1);

    /* Any encountered variables must be primops (since undefined
       variables are detected after parsing). */
    if (matchVar(e, name)) {
        ATerm primOp = state.primOps.get(name);
        if (!primOp)
            throw EvalError(format("impossible: undefined variable `%1%'") % aterm2String(name));
        int arity;
        ATermBlob fun;
        if (!matchPrimOpDef(primOp, arity, fun)) abort();
        if (arity == 0)
            return ((PrimOp) ATgetBlobData(fun)) (state, ATermVector());
        else
            return makePrimOp(arity, fun, ATempty);
    }

    /* Function application. */
    if (matchCall(e, e1, e2)) {

        ATermList formals;
        ATerm pos;
        
        /* Evaluate the left-hand side. */
        e1 = evalExpr(state, e1);

        /* Is it a primop or a function? */
        int arity;
        ATermBlob fun;
        ATermList args;
        if (matchPrimOp(e1, arity, fun, args)) {
            args = ATinsert(args, e2);
            if (ATgetLength(args) == arity) {
                /* Put the arguments in a vector in reverse (i.e.,
                   actual) order. */
                ATermVector args2(arity);
                for (ATermIterator i(args); i; ++i)
                    args2[--arity] = *i;
                return ((PrimOp) ATgetBlobData((ATermBlob) fun))
                    (state, args2);
            } else
                /* Need more arguments, so propagate the primop. */
                return makePrimOp(arity, fun, args);
        }

        else if (matchFunction(e1, formals, e4, pos)) {
            e2 = evalExpr(state, e2);
            try {
                return evalExpr(state, substArgs(state, e4, formals, e2));
            } catch (Error & e) {
                e.addPrefix(format("while evaluating the function at %1%:\n")
                    % showPos(pos));
                throw;
            }
        }
        
        else if (matchFunction1(e1, name, e4, pos)) {
            try {
                ATermMap subs(1);
                subs.set(name, e2);
                return evalExpr(state, substitute(Substitution(0, &subs), e4));
            } catch (Error & e) {
                e.addPrefix(format("while evaluating the function at %1%:\n")
                    % showPos(pos));
                throw;
            }
        }
        
        else throw TypeError(
            format("the left-hand side of the function call is neither a function nor a primop (built-in operation) but %1%")
            % showType(e1));
    }

    /* Attribute selection. */
    if (matchSelect(e, e1, name)) {
        ATerm pos;
        string s1 = aterm2String(name);
        Expr a = queryAttr(evalExpr(state, e1), s1, pos);
        if (!a) throw EvalError(format("attribute `%1%' missing") % s1);
        try {
            return evalExpr(state, a);
        } catch (Error & e) {
            e.addPrefix(format("while evaluating the attribute `%1%' at %2%:\n")
                % s1 % showPos(pos));
            throw;
        }
    }

    /* Mutually recursive sets. */
    ATermList rbnds, nrbnds;
    if (matchRec(e, rbnds, nrbnds))
        return expandRec(e, rbnds, nrbnds);

    /* Conditionals. */
    if (matchIf(e, e1, e2, e3)) {
        if (evalBool(state, e1))
            return evalExpr(state, e2);
        else
            return evalExpr(state, e3);
    }

    /* Assertions. */
    if (matchAssert(e, e1, e2, pos)) {
        if (!evalBool(state, e1))
            throw AssertionError(format("assertion failed at %1%") % showPos(pos));
        return evalExpr(state, e2);
    }

    /* Withs. */
    if (matchWith(e, e1, e2, pos)) {
        ATermMap attrs(128); /* !!! */
        try {
            e1 = evalExpr(state, e1);
            queryAllAttrs(e1, attrs);
        } catch (Error & e) {
            e.addPrefix(format("while evaluating the `with' definitions at %1%:\n")
                % showPos(pos));
            throw;
        }
        try {
            e2 = substitute(Substitution(0, &attrs), e2);
            checkVarDefs(state.primOps, e2);
            return evalExpr(state, e2);
        } catch (Error & e) {
            e.addPrefix(format("while evaluating the `with' body at %1%:\n")
                % showPos(pos));
            throw;
        } 
    }

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
    if (matchOpHasAttr(e, e1, name)) {
        ATermMap attrs(128); /* !!! */
        queryAllAttrs(evalExpr(state, e1), attrs);
        return makeBool(attrs.get(name) != 0);
    }

    /* String or path concatenation. */
    ATermList es = ATempty;
    if (matchOpPlus(e, e1, e2) || matchConcatStrings(e, es)) {
        ATermVector args;
        if (matchOpPlus(e, e1, e2)) {
            args.push_back(e1);
            args.push_back(e2);
        } else
            for (ATermIterator i(es); i; ++i) args.push_back(*i);
        
        try {
            return concatStrings(state, args);
        } catch (Error & e) {
            e.addPrefix(format("in a string concatenation:\n"));
            throw;
        }
    }

    /* Backwards compatability: subpath operator (~). */
    if (matchSubPath(e, e1, e2)) {
        static bool haveWarned = false;
        warnOnce(haveWarned, "the subpath operator (~) is deprecated, use string concatenation (+) instead");
        ATermList context = ATempty;
        bool dummy;
        string s1 = coerceToStringWithContext(state, context, e1, dummy);
        string s2 = coerceToStringWithContext(state, context, e2, dummy);
        return wrapInContext(context, makePath(toATerm(canonPath(s1 + "/" + s2))));
    }

    /* List concatenation. */
    if (matchOpConcat(e, e1, e2)) {
        try {
            ATermList l1 = evalList(state, e1);
            ATermList l2 = evalList(state, e2);
            return makeList(ATconcat(l1, l2));
        } catch (Error & e) {
            e.addPrefix(format("in a list concatenation:\n"));
            throw;
        }
    }

    /* Barf. */
    throw badTerm("invalid expression", e);
}


Expr evalExpr(EvalState & state, Expr e)
{
    checkInterrupt();
    
    startNest(nest, lvlVomit,
        format("evaluating expression: %1%") % e);

    state.nrEvaluated++;

    /* Consult the memo table to quickly get the normal form of
       previously evaluated expressions. */
    Expr nf = state.normalForms.get(e);
    if (nf) {
        if (nf == makeBlackHole())
            throw EvalError("infinite recursion encountered");
        state.nrCached++;
        return nf;
    }

    /* Otherwise, evaluate and memoize. */
    state.normalForms.set(e, makeBlackHole());
    try {
        nf = evalExpr2(state, e);
    } catch (Error & err) {
        debug("removing black hole");
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


Expr strictEvalExpr(EvalState & state, Expr e, bool canonicalise)
{
    e = evalExpr(state, e);

    ATermList as;
    if (matchAttrs(e, as)) {
        ATermList as2 = ATempty;
        for (ATermIterator i(as); i; ++i) {
            ATerm name; Expr e; ATerm pos;
            if (!matchBind(*i, name, e, pos)) abort(); /* can't happen */
            as2 = ATinsert(as2, makeBind(name, strictEvalExpr(state, e, canonicalise),
                               canonicalise ? makeNoPos() : pos));
        }
        /* !!! sort attributes if canonicalise == true */
        return makeAttrs(ATreverse(as2));
    }
    
    ATermList es;
    if (matchList(e, es)) {
        ATermList es2 = ATempty;
        for (ATermIterator i(es); i; ++i)
            es2 = ATinsert(es2, strictEvalExpr(state, *i, canonicalise));
        return makeList(ATreverse(es2));
    }
    
    ATermList formals;
    ATerm body, pos;
    if (matchFunction(e, formals, body, pos)) {
        ATermList formals2 = ATempty;
        
        for (ATermIterator i(formals); i; ++i) {
            Expr name; ValidValues valids; ATerm dummy;
            if (!matchFormal(*i, name, valids, dummy)) abort();

            ATermList valids2;
            if (matchValidValues(valids, valids2)) {
                ATermList valids3 = ATempty;
                for (ATermIterator j(valids2); j; ++j)
                    valids3 = ATinsert(valids3, strictEvalExpr(state, *j, canonicalise));
                valids = makeValidValues(ATreverse(valids3));
            }

            formals2 = ATinsert(formals2, makeFormal(name, valids, dummy));
        }
        return makeFunction(ATreverse(formals2), body,
            canonicalise ? makeNoPos() : pos);
    }
    
    return e;
}


/* Yes, this is a really bad idea... */
extern "C" {
    unsigned long AT_calcAllocatedSize();
}

void printEvalStats(EvalState & state)
{
    bool showStats = getEnv("NIX_SHOW_STATS", "0") != "0";
    printMsg(showStats ? lvlInfo : lvlDebug,
        format("evaluated %1% expressions, %2% cache hits, %3%%% efficiency, used %4% ATerm bytes")
        % state.nrEvaluated % state.nrCached
        % ((float) state.nrCached / (float) state.nrEvaluated * 100)
        % AT_calcAllocatedSize());
    if (showStats)
        printATermMapStats();
}

 
}
