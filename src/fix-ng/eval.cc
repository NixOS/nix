#include "eval.hh"
#include "expr.hh"
#include "parser.hh"
#include "primops.hh"


EvalState::EvalState()
{
    blackHole = ATmake("BlackHole()");
    if (!blackHole) throw Error("cannot build black hole");
    nrEvaluated = nrCached = 0;
}


Expr getAttr(EvalState & state, Expr e, const string & name)
{
}


/* Substitute an argument set into the body of a function. */
static Expr substArgs(Expr body, ATermList formals, Expr arg)
{
    Subs subs;
    Expr undefined = ATmake("Undefined");

    /* Get the formal arguments. */
    while (!ATisEmpty(formals)) {
        char * s;
        if (!ATmatch(ATgetFirst(formals), "<str>", &s))
            abort(); /* can't happen */
        subs[s] = undefined;
        formals = ATgetNext(formals);
    }

    /* Get the actual arguments, and check that they match with the
       formals. */
    Attrs args;
    queryAllAttrs(arg, args);
    for (Attrs::iterator i = args.begin(); i != args.end(); i++) {
        if (subs.find(i->first) == subs.end())
            throw badTerm(format("argument `%1%' not declared") % i->first, arg);
        subs[i->first] = i->second;
    }

    /* Check that all arguments are defined. */
    for (Subs::iterator i = subs.begin(); i != subs.end(); i++)
        if (i->second == undefined)
            throw badTerm(format("formal argument `%1%' missing") % i->first, arg);
    
    return substitute(subs, body);
}


/* Transform a mutually recursive set into a non-recursive set.  Each
   attribute is transformed into an expression that has all references
   to attributes substituted with selection expressions on the
   original set.  E.g., e = `rec {x = f x y, y = x}' becomes `{x = f
   (e.x) (e.y), y = e.x}'. */
ATerm expandRec(ATerm e, ATermList bnds)
{
    /* Create the substitution list. */
    Subs subs;
    ATermList bs = bnds;
    while (!ATisEmpty(bs)) {
        char * s;
        Expr e2;
        if (!ATmatch(ATgetFirst(bs), "Bind(<str>, <term>)", &s, &e2))
            abort(); /* can't happen */
        subs[s] = ATmake("Select(<term>, <str>)", e, s);
        bs = ATgetNext(bs);
    }

    /* Create the non-recursive set. */
    Attrs as;
    bs = bnds;
    while (!ATisEmpty(bs)) {
        char * s;
        Expr e2;
        if (!ATmatch(ATgetFirst(bs), "Bind(<str>, <term>)", &s, &e2))
            abort(); /* can't happen */
        as[s] = substitute(subs, e2);
        bs = ATgetNext(bs);
    }

    return makeAttrs(as);
}


string evalString(EvalState & state, Expr e)
{
    e = evalExpr(state, e);
    char * s;
    if (!ATmatch(e, "Str(<str>)", &s))
        throw badTerm("string expected", e);
    return s;
}


Path evalPath(EvalState & state, Expr e)
{
    e = evalExpr(state, e);
    char * s;
    if (!ATmatch(e, "Path(<str>)", &s))
        throw badTerm("path expected", e);
    return s;
}


Expr evalExpr2(EvalState & state, Expr e)
{
    Expr e1, e2, e3, e4;
    char * s1;

    /* Normal forms. */
    if (ATmatch(e, "Str(<str>)", &s1) ||
        ATmatch(e, "Path(<str>)", &s1) ||
        ATmatch(e, "Uri(<str>)", &s1) ||
        ATmatch(e, "Function([<list>], <term>)", &e1, &e2) ||
        ATmatch(e, "Attrs([<list>])", &e1) ||
        ATmatch(e, "List([<list>])", &e1))
        return e;

    /* Any encountered variables must be undeclared or primops. */
    if (ATmatch(e, "Var(<str>)", &s1)) {
        return e;
    }

    /* Function application. */
    if (ATmatch(e, "Call(<term>, <term>)", &e1, &e2)) {
        
        /* Evaluate the left-hand side. */
        e1 = evalExpr(state, e1);

        /* Is it a primop or a function? */
        if (ATmatch(e1, "Var(<str>)", &s1)) {
            string primop(s1);
            if (primop == "import") return primImport(state, e2);
            if (primop == "derivation") return primDerivation(state, e2);
            else throw badTerm("undefined variable/primop", e1);
        }

        else if (ATmatch(e1, "Function([<list>], <term>)", &e3, &e4)) {
            return evalExpr(state, 
                substArgs(e4, (ATermList) e3, evalExpr(state, e2)));
        }
        
        else throw badTerm("expecting a function or primop", e1);
    }

    /* Attribute selection. */
    if (ATmatch(e, "Select(<term>, <str>)", &e1, &s1)) {
        string name(s1);
        Expr a = queryAttr(evalExpr(state, e1), name);
        if (!a) throw badTerm(format("missing attribute `%1%'") % name, e);
        return evalExpr(state, a);
    }

    /* Mutually recursive sets. */
    ATermList bnds;
    if (ATmatch(e, "Rec([<list>])", &bnds))
        return expandRec(e, (ATermList) bnds);

    /* Let expressions `let {..., body = ...}' are just desugared
       into `(rec {..., body = ...}).body'. */
    if (ATmatch(e, "LetRec(<term>)", &e1))
        return evalExpr(state, ATmake("Select(Rec(<term>), \"body\")", e1));

    /* Barf. */
    throw badTerm("invalid expression", e);
}


Expr evalExpr(EvalState & state, Expr e)
{
    Nest nest(lvlVomit, format("evaluating expression: %1%") % printTerm(e));

    state.nrEvaluated++;

    /* Consult the memo table to quickly get the normal form of
       previously evaluated expressions. */
    NormalForms::iterator i = state.normalForms.find(e);
    if (i != state.normalForms.end()) {
        if (i->second == state.blackHole)
            throw badTerm("infinite recursion", e);
        state.nrCached++;
        return i->second;
    }

    /* Otherwise, evaluate and memoize. */
    state.normalForms[e] = state.blackHole;
    Expr nf = evalExpr2(state, e);
    state.normalForms[e] = nf;
    return nf;
}


Expr evalFile(EvalState & state, const Path & path)
{
    Nest nest(lvlTalkative, format("evaluating file `%1%'") % path);
    Expr e = parseExprFromFile(path);
    return evalExpr(state, e);
}


void printEvalStats(EvalState & state)
{
    debug(format("evaluated %1% expressions, %2% cache hits, %3%%% efficiency")
        % state.nrEvaluated % state.nrCached
        % ((float) state.nrCached / (float) state.nrEvaluated * 100));
}
