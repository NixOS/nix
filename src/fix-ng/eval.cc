#include "eval.hh"
#include "expr.hh"
#include "parser.hh"


EvalState::EvalState()
{
    blackHole = ATmake("BlackHole()");
    if (!blackHole) throw Error("cannot build black hole");
}


Expr evalExpr2(EvalState & state, Expr e)
{
    return e;
}


Expr evalExpr(EvalState & state, Expr e)
{
    Nest nest(lvlVomit, format("evaluating expression: %1%") % printTerm(e));

    /* Consult the memo table to quickly get the normal form of
       previously evaluated expressions. */
    NormalForms::iterator i = state.normalForms.find(e);
    if (i != state.normalForms.end()) {
        if (i->second == state.blackHole)
            throw badTerm("infinite recursion", e);
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
