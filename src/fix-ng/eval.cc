#include "eval.hh"
#include "expr.hh"
#include "parser.hh"
#include "primops.hh"


EvalState::EvalState()
    : normalForms(32768, 75)
{
    blackHole = ATmake("BlackHole()");
    if (!blackHole) throw Error("cannot build black hole");
    nrEvaluated = nrCached = 0;
}


/* Substitute an argument set into the body of a function. */
static Expr substArgs(Expr body, ATermList formals, Expr arg)
{
    ATermMap subs;
    Expr undefined = ATmake("Undefined");

    /* Get the formal arguments. */
    while (!ATisEmpty(formals)) {
        ATerm t = ATgetFirst(formals);
        Expr name, def;
        debug(printTerm(t));
        if (ATmatch(t, "NoDefFormal(<term>)", &name))
            subs.set(name, undefined);
        else if (ATmatch(t, "DefFormal(<term>, <term>)", &name, &def))
            subs.set(name, def);
        else abort(); /* can't happen */
        formals = ATgetNext(formals);
    }

    /* Get the actual arguments, and check that they match with the
       formals. */
    ATermMap args;
    queryAllAttrs(arg, args);
    for (ATermList keys = args.keys(); !ATisEmpty(keys); 
         keys = ATgetNext(keys))
    {
        Expr key = ATgetFirst(keys);
        Expr cur = subs.get(key);
        if (!cur)
            throw badTerm(format("function has no formal argument `%1%'")
                % aterm2String(key), arg);
        subs.set(key, args.get(key));
    }

    /* Check that all arguments are defined. */
    for (ATermList keys = subs.keys(); !ATisEmpty(keys); 
         keys = ATgetNext(keys))
    {
        Expr key = ATgetFirst(keys);
        if (subs.get(key) == undefined)
            throw badTerm(format("formal argument `%1%' missing")
                % aterm2String(key), arg);
    }
    
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
    ATermMap subs;
    ATermList bs = bnds;
    while (!ATisEmpty(bs)) {
        char * s;
        Expr e2;
        if (!ATmatch(ATgetFirst(bs), "Bind(<str>, <term>)", &s, &e2))
            abort(); /* can't happen */
        subs.set(s, ATmake("Select(<term>, <str>)", e, s));
        bs = ATgetNext(bs);
    }

    /* Create the non-recursive set. */
    ATermMap as;
    bs = bnds;
    while (!ATisEmpty(bs)) {
        char * s;
        Expr e2;
        if (!ATmatch(ATgetFirst(bs), "Bind(<str>, <term>)", &s, &e2))
            abort(); /* can't happen */
        as.set(s, substitute(subs, e2));
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


bool evalBool(EvalState & state, Expr e)
{
    e = evalExpr(state, e);
    if (ATmatch(e, "Bool(True)")) return true;
    else if (ATmatch(e, "Bool(False)")) return false;
    else throw badTerm("expecting a boolean", e);
}


Expr evalExpr2(EvalState & state, Expr e)
{
    Expr e1, e2, e3, e4;
    char * s1;

    /* Normal forms. */
    if (ATmatch(e, "Str(<str>)", &s1) ||
        ATmatch(e, "Path(<str>)", &s1) ||
        ATmatch(e, "Uri(<str>)", &s1) ||
        ATmatch(e, "Bool(<term>)", &e1) ||
        ATmatch(e, "Function([<list>], <term>)", &e1, &e2) ||
        ATmatch(e, "Attrs([<list>])", &e1) ||
        ATmatch(e, "List([<list>])", &e1))
        return e;

    /* Any encountered variables must be undeclared or primops. */
    if (ATmatch(e, "Var(<str>)", &s1)) {
        if ((string) s1 == "null") return primNull(state);
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
            if (primop == "toString") return primToString(state, e2);
            if (primop == "baseNameOf") return primBaseNameOf(state, e2);
            if (primop == "isNull") return primIsNull(state, e2);
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

    /* Conditionals. */
    if (ATmatch(e, "If(<term>, <term>, <term>)", &e1, &e2, &e3)) {
        if (evalBool(state, e1))
            return evalExpr(state, e2);
        else
            return evalExpr(state, e3);
    }

    /* Assertions. */
    if (ATmatch(e, "Assert(<term>, <term>)", &e1, &e2)) {
        if (!evalBool(state, e1)) throw badTerm("guard failed", e);
        return evalExpr(state, e2);
    }

    /* Generic equality. */
    if (ATmatch(e, "OpEq(<term>, <term>)", &e1, &e2))
        return makeBool(evalExpr(state, e1) == evalExpr(state, e2));

    /* Generic inequality. */
    if (ATmatch(e, "OpNEq(<term>, <term>)", &e1, &e2))
        return makeBool(evalExpr(state, e1) != evalExpr(state, e2));

    /* Negation. */
    if (ATmatch(e, "OpNot(<term>)", &e1))
        return makeBool(!evalBool(state, e1));

    /* Implication. */
    if (ATmatch(e, "OpImpl(<term>, <term>)", &e1, &e2))
        return makeBool(!evalBool(state, e1) || evalBool(state, e2));

    /* Conjunction (logical AND). */
    if (ATmatch(e, "OpAnd(<term>, <term>)", &e1, &e2))
        return makeBool(evalBool(state, e1) && evalBool(state, e2));

    /* Disjunction (logical OR). */
    if (ATmatch(e, "OpOr(<term>, <term>)", &e1, &e2))
        return makeBool(evalBool(state, e1) || evalBool(state, e2));

    /* Barf. */
    throw badTerm("invalid expression", e);
}


Expr evalExpr(EvalState & state, Expr e)
{
    Nest nest(lvlVomit, format("evaluating expression: %1%") % printTerm(e));

    state.nrEvaluated++;

    /* Consult the memo table to quickly get the normal form of
       previously evaluated expressions. */
    Expr nf = state.normalForms.get(e);
    if (nf) {
        if (nf == state.blackHole)
            throw badTerm("infinite recursion", e);
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
