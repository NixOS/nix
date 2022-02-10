#pragma once

#include "eval.hh"

#define LocalNoInline(f) static f __attribute__((noinline)); f
#define LocalNoInlineNoReturn(f) static f __attribute__((noinline, noreturn)); f

namespace nix {

LocalNoInlineNoReturn(void throwEvalError(const Pos & pos, const char * s, EvalState &evalState))
{
    auto error = EvalError({
        .msg = hintfmt(s),
        .errPos = pos
    });

    if (debuggerHook && !evalState.debugTraces.empty()) {
        DebugTrace &last = evalState.debugTraces.front();
        debuggerHook(&error, last.env, last.expr);
    }

    throw error;
}

LocalNoInlineNoReturn(void throwTypeError(const Pos & pos, const char * s, const Value & v, EvalState &evalState))
{
    auto error = TypeError({
        .msg = hintfmt(s, showType(v)),
        .errPos = pos
    });

    if (debuggerHook && !evalState.debugTraces.empty()) {
        DebugTrace &last = evalState.debugTraces.front();
        debuggerHook(&error, last.env, last.expr);
    }

    throw error;
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
        throwEvalError(getPos(), "infinite recursion encountered", *this);
}


inline void EvalState::forceAttrs(Value & v, const Pos & pos)
{
    forceAttrs(v, [&]() { return pos; });
}


template <typename Callable>
inline void EvalState::forceAttrs(Value & v, Callable getPos)
{
    forceValue(v, getPos);
    if (v.type() != nAttrs)
        throwTypeError(getPos(), "value is %1% while a set was expected", v, *this);
}


inline void EvalState::forceList(Value & v, const Pos & pos)
{
    forceValue(v, pos);
    if (!v.isList())
        throwTypeError(pos, "value is %1% while a list was expected", v, *this);
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
