#pragma once

#include "eval.hh"

#define LocalNoInline(f) static f __attribute__((noinline)); f
#define LocalNoInlineNoReturn(f) static f __attribute__((noinline, noreturn)); f

namespace nix {

LocalNoInlineNoReturn(void throwEvalError(const Pos & pos, const char * s))
{
    throw EvalError({
        .msg = hintfmt(s),
        .errPos = pos
    });
}

LocalNoInlineNoReturn(void throwTypeError(const Pos & pos, const char * s, const Value & v, const std::string & s2))
{
    throw TypeError({
        .msg = hintfmt(s, showType(v), s2),
        .errPos = pos
    });
}


void EvalState::forceValue(Value & v, const Pos & pos)
{
    forceValue(v, [&]() { return pos; });
}


template<typename Callable>
void EvalState::forceValue(Value & v, Callable getPos)
{
    if (v.isThunk()) {
        Env * env = v.thunk.env;
        Expr * expr = v.thunk.expr;
        try {
            v.mkBlackhole();
            //checkInterrupt();
            expr->eval(*this, *env, v);
        } catch (...) {
            v.mkThunk(env, expr);
            throw;
        }
    }
    else if (v.isApp())
        callFunction(*v.app.left, *v.app.right, v, noPos);
    else if (v.isBlackhole())
        throwEvalError(getPos(), "infinite recursion encountered");
}


inline void EvalState::forceAttrs(Value & v, const Pos & pos, const std::string & errorCtx)
{
    forceAttrs(v, [&]() { return pos; }, errorCtx);
}


template <typename Callable>
inline void EvalState::forceAttrs(Value & v, Callable getPos, const std::string & errorCtx)
{
    forceValue(v, getPos);
    if (v.type() != nAttrs)
        throwTypeError(getPos(), "%2%: value is %1% while a set was expected", v, errorCtx);
}


inline void EvalState::forceList(Value & v, const Pos & pos, const std::string & errorCtx)
{
    forceValue(v, pos);
    if (!v.isList())
        throwTypeError(pos, "%2%: value is %1% while a list was expected", v, errorCtx);
}

/* Note: Various places expect the allocated memory to be zeroed. */
inline void * allocBytes(size_t n)
{
    void * p;
#if HAVE_BOEHMGC
    p = GC_MALLOC(n);
#else
    p = calloc(n, 1);
#endif
    if (!p) throw std::bad_alloc();
    return p;
}


}
