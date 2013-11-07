#pragma once

#include "eval.hh"

#define LocalNoInline(f) static f __attribute__((noinline)); f
#define LocalNoInlineNoReturn(f) static f __attribute__((noinline, noreturn)); f

namespace nix {

LocalNoInlineNoReturn(void throwEvalError(const char * s))
{
    throw EvalError(s);
}

LocalNoInlineNoReturn(void throwTypeError(const char * s, const Value & v))
{
    throw TypeError(format(s) % showType(v));
}


void EvalState::forceValue(Value & v)
{
    if (v.type == tThunk) {
        Env * env = v.thunk.env;
        Expr * expr = v.thunk.expr;
        try {
            v.type = tBlackhole;
            //checkInterrupt();
            expr->eval(*this, *env, v);
        } catch (Error & e) {
            v.type = tThunk;
            v.thunk.env = env;
            v.thunk.expr = expr;
            throw;
        }
    }
    else if (v.type == tApp)
        callFunction(*v.app.left, *v.app.right, v);
    else if (v.type == tBlackhole)
        throwEvalError("infinite recursion encountered");
}


inline void EvalState::forceAttrs(Value & v)
{
    forceValue(v);
    if (v.type != tAttrs)
        throwTypeError("value is %1% while a set was expected", v);
}


inline void EvalState::forceList(Value & v)
{
    forceValue(v);
    if (v.type != tList)
        throwTypeError("value is %1% while a list was expected", v);
}

}
