#include "eval.hh"
#include "parser.hh"


EvalState::EvalState()
    : normalForms(32768, 50)
{
    blackHole = ATmake("BlackHole()");
    if (!blackHole) throw Error("cannot build black hole");
    
    nrEvaluated = nrCached = 0;

    addPrimOps();
}


void EvalState::addPrimOp(const string & name,
    unsigned int arity, PrimOp primOp)
{
    primOps.set(name, ATmake("(<int>, <term>)",
        arity, ATmakeBlob(0, (void *) primOp)));
}


/* Substitute an argument set into the body of a function. */
static Expr substArgs(Expr body, ATermList formals, Expr arg)
{
    ATMatcher m;
    ATermMap subs;
    Expr undefined = ATmake("Undefined");

    /* Get the formal arguments. */
    for (ATermIterator i(formals); i; ++i) {
        Expr name, def;
        if (atMatch(m, *i) >> "NoDefFormal" >> name)
            subs.set(name, undefined);
        else if (atMatch(m, *i) >> "DefFormal" >> name >> def)
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
    ATMatcher m;
    ATerm name;
    Expr e2;

    /* Create the substitution list. */
    ATermMap subs;
    for (ATermIterator i(rbnds); i; ++i) {
        if (!(atMatch(m, *i) >> "Bind" >> name >> e2))
            abort(); /* can't happen */
        subs.set(name, ATmake("Select(<term>, <term>)", e, name));
    }
    for (ATermIterator i(nrbnds); i; ++i) {
        if (!(atMatch(m, *i) >> "Bind" >> name >> e2))
            abort(); /* can't happen */
        subs.set(name, e2);
    }

    /* Create the non-recursive set. */
    ATermMap as;
    for (ATermIterator i(rbnds); i; ++i) {
        ATerm pos;
        if (!(atMatch(m, *i) >> "Bind" >> name >> e2 >> pos))
            abort(); /* can't happen */
        as.set(name, ATmake("(<term>, <term>)", substitute(subs, e2), pos));
    }

    /* Copy the non-recursive bindings.  !!! inefficient */
    for (ATermIterator i(nrbnds); i; ++i) {
        ATerm pos;
        if (!(atMatch(m, *i) >> "Bind" >> name >> e2 >> pos))
            abort(); /* can't happen */
        as.set(name, ATmake("(<term>, <term>)", e2, pos));
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
    ATMatcher m;
    string s;
    if (!(atMatch(m, e) >> "Str" >> s))
        throw Error("string expected");
    return s;
}


Path evalPath(EvalState & state, Expr e)
{
    e = evalExpr(state, e);
    ATMatcher m;
    string s;
    if (!(atMatch(m, e) >> "Path" >> s))
        throw Error("path expected");
    return s;
}


bool evalBool(EvalState & state, Expr e)
{
    e = evalExpr(state, e);
    ATMatcher m;
    if (atMatch(m, e) >> "Bool" >> "True") return true;
    else if (atMatch(m, e) >> "Bool" >> "False") return false;
    else throw Error("boolean expected");
}


Expr evalExpr2(EvalState & state, Expr e)
{
    ATMatcher m;
    Expr e1, e2, e3, e4;
    ATerm name, pos;

    /* Normal forms. */
    string cons;
    if (atMatch(m, e) >> cons &&
        (cons == "Str" ||
         cons == "Path" ||
         cons == "SubPath" ||
         cons == "Uri" ||
         cons == "Null" ||
         cons == "Int" ||
         cons == "Bool" ||
         cons == "Function" ||
         cons == "Function1" ||
         cons == "Attrs" ||
         cons == "List" ||
         cons == "PrimOp"))
        return e;

    /* The `Closed' constructor is just a way to prevent substitutions
       into expressions not containing free variables. */
    if (atMatch(m, e) >> "Closed" >> e1)
        return evalExpr(state, e1);

    /* Any encountered variables must be primops (since undefined
       variables are detected after parsing). */
    if (atMatch(m, e) >> "Var" >> name) {
        ATerm primOp = state.primOps.get(name);
        if (!primOp)
            throw Error(format("impossible: undefined variable `%1%'") % name);
        int arity;
        ATerm fun;
        if (!(atMatch(m, primOp) >> "" >> arity >> fun)) abort();
        if (arity == 0)
            return ((PrimOp) ATgetBlobData((ATermBlob) fun))
                (state, ATermVector());
        else
            return ATmake("PrimOp(<int>, <term>, <term>)",
                arity, fun, ATempty);
    }

    /* Function application. */
    if (atMatch(m, e) >> "Call" >> e1 >> e2) {

        ATermList formals;
        ATerm pos;
        
        /* Evaluate the left-hand side. */
        e1 = evalExpr(state, e1);

        /* Is it a primop or a function? */
        int arity;
        ATerm fun;
        ATermList args;
        if (atMatch(m, e1) >> "PrimOp" >> arity >> fun >> args) {
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
                return ATmake("PrimOp(<int>, <term>, <term>)",
                    arity, fun, args);
        }

        else if (atMatch(m, e1) >> "Function" >> formals >> e4 >> pos) {
            e2 = evalExpr(state, e2);
            try {
                return evalExpr(state, substArgs(e4, formals, e2));
            } catch (Error & e) {
                throw Error(format("while evaluating the function at %1%:\n%2%")
                    % showPos(pos) % e.msg());
            }
        }
        
        else if (atMatch(m, e1) >> "Function1" >> name >> e4 >> pos) {
            try {
                ATermMap subs;
                subs.set(name, e2);
                return evalExpr(state, substitute(subs, e4));
            } catch (Error & e) {
                throw Error(format("while evaluating the function at %1%:\n%2%")
                    % showPos(pos) % e.msg());
            }
        }
        
        else throw Error("function or primop expected in function call");
    }

    /* Attribute selection. */
    string s1;
    if (atMatch(m, e) >> "Select" >> e1 >> s1) {
        ATerm pos;
        Expr a = queryAttr(evalExpr(state, e1), s1, pos);
        if (!a) throw Error(format("attribute `%1%' missing") % s1);
        try {
            return evalExpr(state, a);
        } catch (Error & e) {
            throw Error(format("while evaluating the attribute `%1%' at %2%:\n%3%")
                % s1 % showPos(pos) % e.msg());
        }
    }

    /* Mutually recursive sets. */
    ATermList rbnds, nrbnds;
    if (atMatch(m, e) >> "Rec" >> rbnds >> nrbnds)
        return expandRec(e, rbnds, nrbnds);

    /* Conditionals. */
    if (atMatch(m, e) >> "If" >> e1 >> e2 >> e3) {
        if (evalBool(state, e1))
            return evalExpr(state, e2);
        else
            return evalExpr(state, e3);
    }

    /* Assertions. */
    if (atMatch(m, e) >> "Assert" >> e1 >> e2 >> pos) {
        if (!evalBool(state, e1))
            throw Error(format("assertion failed at %1%") % showPos(pos));
        return evalExpr(state, e2);
    }

    /* Withs. */
    if (atMatch(m, e) >> "With" >> e1 >> e2 >> pos) {
        ATermMap attrs;
        try {
            e1 = evalExpr(state, e1);
            queryAllAttrs(e1, attrs);
        } catch (Error & e) {
            throw Error(format("while evaluating the `with' definitions at %1%:\n%2%")
                % showPos(pos) % e.msg());
        }
        try {
            e2 = substitute(attrs, e2);
            checkVarDefs(state.primOps, e2);
            return evalExpr(state, e2);
        } catch (Error & e) {
            throw Error(format("while evaluating the `with' body at %1%:\n%2%")
                % showPos(pos) % e.msg());
        } 
    }

    /* Generic equality. */
    if (atMatch(m, e) >> "OpEq" >> e1 >> e2)
        return makeBool(evalExpr(state, e1) == evalExpr(state, e2));

    /* Generic inequality. */
    if (atMatch(m, e) >> "OpNEq" >> e1 >> e2)
        return makeBool(evalExpr(state, e1) != evalExpr(state, e2));

    /* Negation. */
    if (atMatch(m, e) >> "OpNot" >> e1)
        return makeBool(!evalBool(state, e1));

    /* Implication. */
    if (atMatch(m, e) >> "OpImpl" >> e1 >> e2)
        return makeBool(!evalBool(state, e1) || evalBool(state, e2));

    /* Conjunction (logical AND). */
    if (atMatch(m, e) >> "OpAnd" >> e1 >> e2)
        return makeBool(evalBool(state, e1) && evalBool(state, e2));

    /* Disjunction (logical OR). */
    if (atMatch(m, e) >> "OpOr" >> e1 >> e2)
        return makeBool(evalBool(state, e1) || evalBool(state, e2));

    /* Attribute set update (//). */
    if (atMatch(m, e) >> "OpUpdate" >> e1 >> e2)
        return updateAttrs(evalExpr(state, e1), evalExpr(state, e2));

    /* Attribute existence test (?). */
    if (atMatch(m, e) >> "OpHasAttr" >> e1 >> name) {
        ATermMap attrs;
        queryAllAttrs(evalExpr(state, e1), attrs);
        return makeBool(attrs.get(name) != 0);
    }

    /* String or path concatenation. */
    if (atMatch(m, e) >> "OpPlus" >> e1 >> e2) {
        e1 = evalExpr(state, e1);
        e2 = evalExpr(state, e2);
        string s1, s2;
        if (atMatch(m, e1) >> "Str" >> s1 &&
            atMatch(m, e2) >> "Str" >> s2)
            return makeString(s1 + s2);
        else if (atMatch(m, e1) >> "Path" >> s1 &&
            atMatch(m, e2) >> "Path" >> s2)
            return makePath(canonPath(s1 + "/" + s2));
        else throw Error("wrong argument types in `+' operator");
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
        throw Error(format("while evaluating the file `%1%':\n%2%")
            % path % e.msg());
    }
}


void printEvalStats(EvalState & state)
{
    debug(format("evaluated %1% expressions, %2% cache hits, %3%%% efficiency")
        % state.nrEvaluated % state.nrCached
        % ((float) state.nrCached / (float) state.nrEvaluated * 100));
}
