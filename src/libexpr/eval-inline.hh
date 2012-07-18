#pragma once

#include "eval.hh"

#define LocalNoInline(f) static f __attribute__((noinline)); f
#define LocalNoInlineNoReturn(f) static f __attribute__((noinline, noreturn)); f

namespace nix {

LocalNoInlineNoReturn(void throwEvalError(const char * s))
{
    throw EvalError(s);
}

LocalNoInlineNoReturn(void throwTypeError(const char * s, const string & s2))
{
    throw TypeError(format(s) % s2);
}


void EvalState::forceValue(Value & v)
{
    if (v.type == tThunk) {
        ValueType saved = v.type;
        try {
            v.type = tBlackhole;
            //checkInterrupt();
            v.thunk.expr->eval(*this, *v.thunk.env, v);
        } catch (Error & e) {
            v.type = saved;
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
        throwTypeError("value is %1% while an attribute set was expected", showType(v));
}


inline void EvalState::forceList(Value & v)
{
    forceValue(v);
    if (v.type != tList)
        throwTypeError("value is %1% while a list was expected", showType(v));
}

}
