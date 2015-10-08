#pragma once

#include "eval.hh"

namespace nix {

void EvalState::forceValue(Value & v, const Pos & pos)
{
    if(!(v.pos))
        v.pos = pos;
    if (v.type == tThunk) {
        Env * env = v.thunk.env;
        Expr * expr = v.thunk.expr;
        try {
            v.type = tBlackhole;
            //checkInterrupt();
            expr->eval(*this, *env, v);
        } catch (Error & e) {
            addErrorPrefix(e, "while evaluating %1%%2%:\n", v, (v.pos == pos) ? noPos : pos);
            v.type = tThunk;
            v.thunk.env = env;
            v.thunk.expr = expr;
            throw;
        }
    }
    else if (v.type == tApp)
        callFunction(*v.app.left, *v.app.right, v, pos);
    else if (v.type == tBlackhole)
        throwEvalError("infinite recursion encountered%1%", pos);
}


inline void EvalState::forceAttrs(Value & v, const Pos & pos)
{
    forceValue(v, pos);
    if (v.type != tAttrs)
        throwTypeError("value is %1% while a set was expected%2%", v, pos);
}


inline void EvalState::forceList(Value & v, const Pos & pos)
{
    forceValue(v, pos);
    if (!v.isList())
        throwTypeError("value is %1% while a list was expected%2%", v, pos);
}


}
