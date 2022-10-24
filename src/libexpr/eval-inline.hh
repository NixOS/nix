#pragma once

#include "eval.hh"

namespace nix {

/* Note: Various places expect the allocated memory to be zeroed. */
[[gnu::always_inline]]
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


[[gnu::always_inline]]
Value * EvalState::allocValue()
{
#if HAVE_BOEHMGC
    /* We use the boehm batch allocator to speed up allocations of Values (of which there are many).
       GC_malloc_many returns a linked list of objects of the given size, where the first word
       of each object is also the pointer to the next object in the list. This also means that we
       have to explicitly clear the first word of every object we take. */
    if (!*valueAllocCache) {
        *valueAllocCache = GC_malloc_many(sizeof(Value));
        if (!*valueAllocCache) throw std::bad_alloc();
    }

    /* GC_NEXT is a convenience macro for accessing the first word of an object.
       Take the first list item, advance the list to the next item, and clear the next pointer. */
    void * p = *valueAllocCache;
    *valueAllocCache = GC_NEXT(p);
    GC_NEXT(p) = nullptr;
#else
    void * p = allocBytes(sizeof(Value));
#endif

    nrValues++;
    return (Value *) p;
}


[[gnu::always_inline]]
Env & EvalState::allocEnv(size_t size)
{
    nrEnvs++;
    nrValuesInEnvs += size;

    Env * env;

#if HAVE_BOEHMGC
    if (size == 1) {
        /* see allocValue for explanations. */
        if (!*env1AllocCache) {
            *env1AllocCache = GC_malloc_many(sizeof(Env) + sizeof(Value *));
            if (!*env1AllocCache) throw std::bad_alloc();
        }

        void * p = *env1AllocCache;
        *env1AllocCache = GC_NEXT(p);
        GC_NEXT(p) = nullptr;
        env = (Env *) p;
    } else
#endif
        env = (Env *) allocBytes(sizeof(Env) + size * sizeof(Value *));

    env->type = Env::Plain;

    /* We assume that env->values has been cleared by the allocator; maybeThunk() and lookupVar fromWith expect this. */

    return *env;
}


[[gnu::always_inline]]
void EvalState::forceValue(Value & v, const PosIdx pos)
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
        error("infinite recursion encountered").atPos(getPos()).template debugThrow<EvalError>();
}


[[gnu::always_inline]]
inline void EvalState::forceAttrs(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    forceAttrs(v, [&]() { return pos; }, errorCtx);
}


template <typename Callable>
[[gnu::always_inline]]
inline void EvalState::forceAttrs(Value & v, Callable getPos, std::string_view errorCtx)
{
    forceValue(v, noPos);
    if (v.type() != nAttrs) {
        PosIdx pos = getPos();
        error("value is %1% while a set was expected", showType(v)).withTrace(pos, errorCtx).debugThrow<TypeError>();
    }
}


[[gnu::always_inline]]
inline void EvalState::forceList(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    forceValue(v, noPos);
    if (!v.isList()) {
        error("value is %1% while a list was expected", showType(v)).withTrace(pos, errorCtx).debugThrow<TypeError>();
    }
}


}
