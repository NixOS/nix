#pragma once
///@file

#include "config-expr.hh"
#include "print.hh"
#include "eval.hh"
#include "eval-error.hh"
#include "eval-settings.hh"
#include <cassert>

namespace nix {

/**
 * Note: Various places expect the allocated memory to be zeroed.
 */
[[nodiscard]]
[[using gnu: hot, always_inline, returns_nonnull, malloc, alloc_size(1)]]
inline void * allocBytes(size_t n)
{
    void * p = nullptr;

    if constexpr (HAVE_BOEHMGC) {
        p = GC_MALLOC(n);
    } else {
        p = calloc(n, 1);
    }

    if (p == nullptr) [[unlikely]] {
        throw std::bad_alloc();
    }
    return p;
}


[[nodiscard]]
[[using gnu: hot, always_inline, returns_nonnull, malloc]]
inline Value * EvalState::allocValue()
{
    void * p = nullptr;

    if constexpr (HAVE_BOEHMGC) {
        /* We use the boehm batch allocator to speed up allocations of Values (of which there are many).
       GC_malloc_many returns a linked list of objects of the given size, where the first word
       of each object is also the pointer to the next object in the list. This also means that we
       have to explicitly clear the first word of every object we take. */
        if (*valueAllocCache == nullptr) [[unlikely]] {
            *valueAllocCache = GC_malloc_many(sizeof(Value));
        }

        if (*valueAllocCache == nullptr) [[unlikely]] {
            throw std::bad_alloc();
        }

        /* GC_NEXT is a convenience macro for accessing the first word of an object.
        Take the first list item, advance the list to the next item, and clear the next pointer. */
        p = *valueAllocCache;
        *valueAllocCache = GC_NEXT(p);
        GC_NEXT(p) = nullptr;
    } else {
        p = allocBytes(sizeof(Value));
    }

    nrValues++;
    return static_cast<Value *>(p);
}


[[nodiscard]]
[[using gnu: hot, always_inline, returns_nonnull, malloc]]
inline ValueList * EvalState::allocList()
{
    void * p = nullptr;

    // See the comment in allocValue for an explanation of this block.
    if constexpr (HAVE_BOEHMGC) {
        if (*listAllocCache == nullptr) [[unlikely]] {
            *listAllocCache = GC_malloc_many(sizeof(ValueList));
        }

        if (*listAllocCache == nullptr) [[unlikely]] {
            throw std::bad_alloc();
        }

        p = *listAllocCache;
        *listAllocCache = GC_NEXT(p);
        GC_NEXT(p) = nullptr;
    } else {
        p = allocBytes(sizeof(ValueList));
    }

    return ::new (p) ValueList;
}


[[nodiscard]]
[[using gnu: hot, always_inline, returns_nonnull, malloc]]
inline Env * EvalState::allocEnv(size_t size)
{
    nrEnvs++;
    nrValuesInEnvs += size;

    void * p = nullptr;

    if constexpr (HAVE_BOEHMGC) {
        if (size == 1) {
            /* see allocValue for explanations. */
            if (*env1AllocCache == nullptr) [[unlikely]] {
                *env1AllocCache = GC_malloc_many(sizeof(Env) + sizeof(Value *));
            }

            if (*env1AllocCache == nullptr) [[unlikely]] {
                throw std::bad_alloc();
            }

            p = *env1AllocCache;
            *env1AllocCache = GC_NEXT(p);
            GC_NEXT(p) = nullptr;
        }
    }

    if (p == nullptr) {
        p = allocBytes(sizeof(Env) + size * sizeof(Value *));
    }

    /* We assume that env->values has been cleared by the allocator; maybeThunk() and lookupVar fromWith expect this. */
    return static_cast<Env *>(p);
}


[[using gnu: hot, always_inline]]
inline void EvalState::forceValue(Value & v, const PosIdx pos)
{
    if (v.isThunk()) {
        Env * env = v.payload.thunk.env;
        Expr * expr = v.payload.thunk.expr;
        try {
            v.mkBlackhole();
            //checkInterrupt();
            expr->eval(*this, *env, v);
        } catch (...) {
            v.mkThunk(env, expr);
            tryFixupBlackHolePos(v, pos);
            throw;
        }
    }
    else if (v.isApp()) {
        callFunction(*v.payload.app.left, *v.payload.app.right, v, pos);
    }
    // TODO(@connorbaker): Somewhere, somehow, an uninitialized value is being forced.
    assert(v.isValid());
}


[[using gnu: hot, always_inline]]
inline void EvalState::forceAttrs(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    forceAttrs(v, [&]() { return pos; }, errorCtx);
}


template <typename Callable>
[[using gnu: hot, always_inline]]
inline void EvalState::forceAttrs(Value & v, Callable getPos, std::string_view errorCtx)
{
    PosIdx pos = getPos();
    forceValue(v, pos);
    if (v.type() != nAttrs) [[ unlikely ]] {
        error<TypeError>(
            "expected a set but found %1%: %2%",
            showType(v),
            ValuePrinter(*this, v, errorPrintOptions)
        ).withTrace(pos, errorCtx).debugThrow();
    }
}


[[using gnu: hot, always_inline]]
inline void EvalState::forceList(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    forceValue(v, pos);
    if (!v.isList()) [[ unlikely ]] {
        error<TypeError>(
            "expected a list but found %1%: %2%",
            showType(v),
            ValuePrinter(*this, v, errorPrintOptions)
        ).withTrace(pos, errorCtx).debugThrow();
    }
}


[[using gnu: hot, always_inline]]
inline CallDepth EvalState::addCallDepth(const PosIdx pos) {
    if (callDepth > settings.maxCallDepth) [[unlikely ]] {
        error<EvalError>("stack overflow; max-call-depth exceeded").atPos(pos).debugThrow();
    }

    return {callDepth};
};

}
