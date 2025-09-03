#pragma once
///@file

#include "nix/expr/print.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-error.hh"
#include "nix/expr/eval-settings.hh"

namespace nix {

/**
 * Note: Various places expect the allocated memory to be zeroed.
 */
[[gnu::always_inline]]
inline void * allocBytes(size_t n)
{
    void * p;
#if NIX_USE_BOEHMGC
    p = GC_MALLOC(n);
#else
    p = calloc(n, 1);
#endif
    if (!p)
        throw std::bad_alloc();
    return p;
}

[[gnu::always_inline]]
Value * EvalState::allocValue()
{
#if NIX_USE_BOEHMGC
    /* We use the boehm batch allocator to speed up allocations of Values (of which there are many).
       GC_malloc_many returns a linked list of objects of the given size, where the first word
       of each object is also the pointer to the next object in the list. This also means that we
       have to explicitly clear the first word of every object we take. */
    thread_local static std::shared_ptr<void *> valueAllocCache{
        std::allocate_shared<void *>(traceable_allocator<void *>(), nullptr)};

    if (!*valueAllocCache) {
        *valueAllocCache = GC_malloc_many(sizeof(Value));
        if (!*valueAllocCache)
            throw std::bad_alloc();
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

#if NIX_USE_BOEHMGC
    if (size == 1) {
        /* see allocValue for explanations. */
        thread_local static std::shared_ptr<void *> env1AllocCache{
            std::allocate_shared<void *>(traceable_allocator<void *>(), nullptr)};

        if (!*env1AllocCache) {
            *env1AllocCache = GC_malloc_many(sizeof(Env) + sizeof(Value *));
            if (!*env1AllocCache)
                throw std::bad_alloc();
        }

        void * p = *env1AllocCache;
        *env1AllocCache = GC_NEXT(p);
        GC_NEXT(p) = nullptr;
        env = (Env *) p;
    } else
#endif
        env = (Env *) allocBytes(sizeof(Env) + size * sizeof(Value *));

    /* We assume that env->values has been cleared by the allocator; maybeThunk() and lookupVar fromWith expect this. */

    return *env;
}

template<std::size_t ptrSize>
void ValueStorage<ptrSize, std::enable_if_t<detail::useBitPackedValueStorage<ptrSize>>>::force(
    EvalState & state, PosIdx pos)
{
    auto p0_ = p0.load(std::memory_order_acquire);

    auto pd = static_cast<PrimaryDiscriminator>(p0_ & discriminatorMask);

    if (pd == pdThunk) {
        try {
            // The value we get here is only valid if we can set the
            // thunk to pending.
            auto p1_ = p1;

            // Atomically set the thunk to "pending".
            if (!p0.compare_exchange_strong(p0_, pdPending, std::memory_order_acquire, std::memory_order_acquire)) {
                pd = static_cast<PrimaryDiscriminator>(p0_ & discriminatorMask);
                if (pd == pdPending || pd == pdAwaited) {
                    // The thunk is already "pending" or "awaited", so
                    // we need to wait for it.
                    p0_ = waitOnThunk(state, pd == pdAwaited);
                    goto done;
                }
                assert(pd != pdThunk);
                // Another thread finished this thunk, no need to wait.
                goto done;
            }

            bool isApp = p1_ & discriminatorMask;
            if (isApp) {
                auto left = untagPointer<Value *>(p0_);
                auto right = untagPointer<Value *>(p1_);
                state.callFunction(*left, *right, (Value &) *this, pos);
            } else {
                auto env = untagPointer<Env *>(p0_);
                auto expr = untagPointer<Expr *>(p1_);
                expr->eval(state, *env, (Value &) *this);
            }
        } catch (...) {
            state.tryFixupBlackHolePos((Value &) *this, pos);
            setStorage(new Value::Failed{.ex = std::current_exception()});
            throw;
        }
    }

    else if (pd == pdPending || pd == pdAwaited)
        p0_ = waitOnThunk(state, pd == pdAwaited);

done:
    if (InternalType(p0_ & 0xff) == tFailed)
        std::rethrow_exception((std::bit_cast<Failed *>(p1))->ex);
}

[[gnu::always_inline]]
inline void EvalState::forceAttrs(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    forceAttrs(v, [&]() { return pos; }, errorCtx);
}

template<typename Callable>
[[gnu::always_inline]]
inline void EvalState::forceAttrs(Value & v, Callable getPos, std::string_view errorCtx)
{
    PosIdx pos = getPos();
    forceValue(v, pos);
    if (v.type() != nAttrs) {
        error<TypeError>("expected a set but found %1%: %2%", showType(v), ValuePrinter(*this, v, errorPrintOptions))
            .withTrace(pos, errorCtx)
            .debugThrow();
    }
}

[[gnu::always_inline]]
inline void EvalState::forceList(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    forceValue(v, pos);
    if (!v.isList()) {
        error<TypeError>("expected a list but found %1%: %2%", showType(v), ValuePrinter(*this, v, errorPrintOptions))
            .withTrace(pos, errorCtx)
            .debugThrow();
    }
}

[[gnu::always_inline]]
inline CallDepth EvalState::addCallDepth(const PosIdx pos)
{
    if (callDepth > settings.maxCallDepth)
        error<EvalBaseError>("stack overflow; max-call-depth exceeded").atPos(pos).debugThrow();

    return CallDepth(callDepth);
};

} // namespace nix
