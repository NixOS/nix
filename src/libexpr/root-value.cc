#include "nix/expr/root-value.hh"
#include "nix/expr/eval-gc.hh"
#include "nix/util/sync.hh"

#include <vector>

namespace nix {

#if NIX_USE_BOEHMGC
/* The pool of root value slots. Slots are carved out of uncollectable
   slabs, which are permanently part of the GC root set, and recycled
   through this free list. This avoids doing a GC_MALLOC_UNCOLLECTABLE()
   / GC_FREE() pair per root value, which requires taking the global GC
   allocation lock — a significant source of contention during parallel
   evaluation. Never destroyed since root values held by statics may be
   released after us during shutdown. */
static auto & rootValuePool = *new Sync<std::vector<Value **>>;
#endif

Value ** allocRootValueSlot(Value * v)
{
#if NIX_USE_BOEHMGC
    Value ** slot;
    {
        auto pool(rootValuePool.lock());
        if (pool->empty()) {
            constexpr size_t slabSize = 4096;
            auto slab = (Value **) GC_MALLOC_UNCOLLECTABLE(slabSize * sizeof(Value *));
            if (!slab)
                throw std::bad_alloc();
            pool->reserve(slabSize);
            for (size_t i = 0; i < slabSize; ++i)
                pool->push_back(slab + i);
        }
        slot = pool->back();
        pool->pop_back();
    }

    *slot = v;

    return slot;
#else
    return new Value *(v);
#endif
}

void freeRootValueSlot(Value ** slot)
{
#if NIX_USE_BOEHMGC
    /* Clear the slot so it doesn't keep the value alive. */
    *slot = nullptr;
    rootValuePool.lock()->push_back(slot);
#else
    delete slot;
#endif
}

RootValue allocRootValue(Value * v)
{
    return RootValue(allocRootValueSlot(v), freeRootValueSlot);
}

} // namespace nix
