#include "nix/expr/root-value.hh"
#include "nix/expr/eval-gc.hh"
#include "nix/util/sync.hh"

namespace nix {

#if NIX_USE_BOEHMGC

namespace {
/**
 * A root value slot: either in use (rooting a value) or on the
 * freelist. Slots are carved out of uncollectable slabs, which are
 * permanently part of the GC root set. This avoids doing a
 * GC_MALLOC_UNCOLLECTABLE() / GC_FREE() pair per root value, which
 * requires taking the global GC allocation lock — a significant
 * source of contention during parallel evaluation.
 *
 * GC safety: the slabs are conservatively scanned. In-use slots
 * contain a `Value *`, which roots the value. Free slots contain a
 * pointer to the next free slot (or null), i.e. a pointer into a
 * slab, which is scanned harmlessly since slabs are never freed
 * anyway.
 */
union Slot
{
    Value * value;
    Slot * nextFree;
};

static_assert(sizeof(Slot) == sizeof(Value *));
} // namespace

/* Head of the freelist of slots. Never destroyed since root values
   held by statics may be released after us during shutdown. */
static auto & freeSlots = *new Sync<Slot *>{nullptr};

#endif

Value ** allocRootValueSlot(Value * v)
{
#if NIX_USE_BOEHMGC
    Slot * slot;
    {
        auto head(freeSlots.lock());
        if (!*head) {
            constexpr size_t slabSize = 4096;
            auto slab = (Slot *) GC_MALLOC_UNCOLLECTABLE(slabSize * sizeof(Slot));
            if (!slab)
                throw std::bad_alloc();
            for (size_t i = 0; i + 1 < slabSize; ++i)
                slab[i].nextFree = &slab[i + 1];
            slab[slabSize - 1].nextFree = nullptr;
            *head = slab;
        }
        slot = *head;
        *head = slot->nextFree;
    }

    slot->value = v;

    return &slot->value;
#else
    return new Value *(v);
#endif
}

void freeRootValueSlot(Value ** slot)
{
#if NIX_USE_BOEHMGC
    /* Note: writing `nextFree` overwrites the `Value *`, so this also
       stops the slot from keeping the value alive. */
    auto s = reinterpret_cast<Slot *>(slot);
    auto head(freeSlots.lock());
    s->nextFree = *head;
    *head = s;
#else
    delete slot;
#endif
}

RootValue allocRootValue(Value * v)
{
    return RootValue(allocRootValueSlot(v), freeRootValueSlot);
}

} // namespace nix
