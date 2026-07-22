#pragma once
///@file

#include <memory>
#include <utility>

namespace nix {

struct Value;

/**
 * A move-only handle rooting a Value, i.e. keeping it and everything
 * reachable from it alive across garbage collections. Prefer this
 * over `RootValue` unless the handle must be copyable (e.g. when it's
 * captured in a `std::function`-backed lambda).
 */
class RootValue
{
    Value ** slot = nullptr;

    /**
     * Clear the given slot and return it to the root value pool.
     */
    void freeRootValueSlot();

public:
    RootValue() = default;

    /**
     * Allocate a slot from the root value pool, i.e. a GC-visible
     * `Value *` cell that keeps the value it points to alive across
     * garbage collections. Use `RootValue`/`RootValue` rather than
     * calling this directly.
     */
    explicit RootValue(Value * v);

    RootValue(const RootValue &) = delete;
    RootValue & operator=(const RootValue &) = delete;

    RootValue(RootValue && other) noexcept
        : slot(std::exchange(other.slot, nullptr))
    {
    }

    RootValue & operator=(RootValue && other) noexcept
    {
        if (slot)
            freeRootValueSlot();
        slot = std::exchange(other.slot, nullptr);
        return *this;
    }

    ~RootValue()
    {
        reset();
    }

    /**
     * Release the slot, i.e. stop rooting the value.
     */
    void reset()
    {
        if (slot)
            freeRootValueSlot();
    }

    Value *& operator*() const
    {
        return *slot;
    }

    explicit operator bool() const
    {
        return slot != nullptr;
    }
};

} // namespace nix
