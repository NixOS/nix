#include "eval.hh"
#include "parser.hh"
#include "nixexpr-ast.hh"


EvalState::EvalState()
    : normalForms(32768, 50)
{
    blackHole = makeBlackHole();
    
    nrEvaluated = nrCached = 0;

    initNixExprHelpers();

    addPrimOps();
}


void EvalState::addPrimOp(const string & name,
    unsigned int arity, PrimOp primOp)
{
    primOps.set(name, makePrimOpDef(arity, ATmakeBlob(0, (void *) primOp)));
}


/* Substitute an argument set into the body of a function. */
static Expr substArgs(Expr body, ATermList formals, Expr arg)
{
    ATermMap subs;
    Expr undefined = makeUndefined();

    /* Get the formal arguments. */
    for (ATermIterator i(formals); i; ++i) {
        Expr name, def;
        if (matchNoDefFormal(*i, name))
            subs.set(name, undefined);
        else if (matchDefFormal(*i, name, def))
            subs.set(name, def);
        else abort(); /* can't happen */
    }

    /* Get the actual arguments, and check that they match with the
       formals. */
    ATermMap args;
    queryAllAttrs(arg, args);
    for (ATermIterator i(args.keys()); i; ++i) {
        Expr key = *i;
        Expr cur = subs.get(key);
        if (!cur)
            throw Error(format("unexpected function argument `%1%'")
                % aterm2String(key));
        subs.set(key, args.get(key));
    }

    /* Check that all arguments are defined. */
    for (ATermIterator i(subs.keys()); i; ++i)
        if (subs.get(*i) == undefined)
            throw Error(format("required function argument `%1%' missing")
                % aterm2String(*i));
    
    return substitute(subs, body);
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
    ATermMap subs;
    for (ATermIterator i(rbnds); i; ++i) {
        if (!matchBind(*i, name, e2, pos)) abort(); /* can't happen */
        subs.set(name, makeSelect(e, name));
    }
    for (ATermIterator i(nrbnds); i; ++i) {
        if (!matchBind(*i, name, e2, pos)) abort(); /* can't happen */
        subs.set(name, e2);
    }

    /* Create the non-recursive set. */
    ATermMap as;
    for (ATermIterator i(rbnds); i; ++i) {
        if (!matchBind(*i, name, e2, pos)) abort(); /* can't happen */
        as.set(name, makeAttrRHS(substitute(subs, e2), pos));
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

    ATermMap attrs;
    queryAllAttrs(e1, attrs, true);
    queryAllAttrs(e2, attrs, true);

    return makeAttrs(attrs);
}


string evalString(EvalState & state, Expr e)
{
    e = evalExpr(state, e);
    ATerm s;
    if (!matchStr(e, s)) throw Error("string expected");
    return aterm2String(s);
}


Path evalPath(EvalState & state, Expr e)
{
    e = evalExpr(state, e);
    ATerm s;
    if (!matchPath(e, s)) throw Error("path expected");
    return aterm2String(s);
}


bool evalBool(EvalState & state, Expr e)
{
    e = evalExpr(state, e);
    if (e == eTrue) return true;
    else if (e == eFalse) return false;
    else throw Error("boolean expected");
}


ATermList evalList(EvalState & state, Expr e)
{
    e = evalExpr(state, e);
    ATermList list;
    if (!matchList(e, list)) throw Error("list expected");
    return list;
}


Expr evalExpr2(EvalState & state, Expr e)
{
    Expr e1, e2, e3, e4;
    ATerm name, pos;
    AFun sym = ATgetAFun(e);

    /* Normal forms. */
    if (sym == symStr ||
        sym == symPath ||
        sym == symSubPath ||
        sym == symUri ||
        sym == symNull ||
        sym == symInt ||
        sym == symBool ||
        sym == symFunction ||
        sym == symFunction1 ||
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
    if (matchVar(e, name)) {
        ATerm primOp = state.primOps.get(name);
        if (!primOp)
            throw Error(format("impossible: undefined variable `%1%'") % name);
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
                return evalExpr(state, substArgs(e4, formals, e2));
            } catch (Error & e) {
                e.addPrefix(format("while evaluating the function at %1%:\n")
                    % showPos(pos));
                throw;
            }
        }
        
        else if (matchFunction1(e1, name, e4, pos)) {
            try {
                ATermMap subs;
                subs.set(name, e2);
                return evalExpr(state, substitute(subs, e4));
            } catch (Error & e) {
                e.addPrefix(format("while evaluating the function at %1%:\n")
                    % showPos(pos));
                throw;
            }
        }
        
        else throw Error("function or primop expected in function call");
    }

    /* Attribute selection. */
    if (matchSelect(e, e1, name)) {
        ATerm pos;
        string s1 = aterm2String(name);
        Expr a = queryAttr(evalExpr(state, e1), s1, pos);
        if (!a) throw Error(format("attribute `%1%' missing") % s1);
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
        ATermMap attrs;
        try {
            e1 = evalExpr(state, e1);
            queryAllAttrs(e1, attrs);
        } catch (Error & e) {
            e.addPrefix(format("while evaluating the `with' definitions at %1%:\n")
                % showPos(pos));
            throw;
        }
        try {
            e2 = substitute(attrs, e2);
            checkVarDefs(state.primOps, e2);
            return evalExpr(state, e2);
        } catch (Error & e) {
            e.addPrefix(format("while evaluating the `with' body at %1%:\n")
                % showPos(pos));
            throw;
        } 
    }

    /* Generic equality. */
    if (matchOpEq(e, e1, e2))
        return makeBool(evalExpr(state, e1) == evalExpr(state, e2));

    /* Generic inequality. */
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
        ATermMap attrs;
        queryAllAttrs(evalExpr(state, e1), attrs);
        return makeBool(attrs.get(name) != 0);
    }

    /* String or path concatenation. */
    if (matchOpPlus(e, e1, e2)) {
        e1 = evalExpr(state, e1);
        e2 = evalExpr(state, e2);
        ATerm s1, s2;
        if (matchStr(e1, s1) && matchStr(e2, s2))
            return makeStr(toATerm(
                (string) aterm2String(s1) + (string) aterm2String(s2)));
        else if (matchPath(e1, s1) && matchPath(e2, s2))
            return makePath(toATerm(canonPath(
                (string) aterm2String(s1) + "/" + (string) aterm2String(s2))));
        else throw Error("wrong argument types in `+' operator");
    }

    /* List concatenation. */
    if (matchOpConcat(e, e1, e2)) {
        ATermList l1 = evalList(state, e1);
        ATermList l2 = evalList(state, e2);
        return makeList(ATconcat(l1, l2));
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
        if (nf == state.blackHole)
            throw Error("infinite recursion encountered");
        state.nrCached++;
        return nf;
    }

    /* Otherwise, evaluate and memoize. */
    state.normalForms.set(e, state.blackHole);
    nf = evalExpr2(state, e);
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


void printEvalStats(EvalState & state)
{
    debug(format("evaluated %1% expressions, %2% cache hits, %3%%% efficiency")
        % state.nrEvaluated % state.nrCached
        % ((float) state.nrCached / (float) state.nrEvaluated * 100));
}
