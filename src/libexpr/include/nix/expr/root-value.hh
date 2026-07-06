#pragma once
///@file

#include <memory>
#include <utility>

namespace nix {

struct Value;

/**
 * Allocate a slot from the root value pool, i.e. a GC-visible
 * `Value *` cell that keeps the value it points to alive across
 * garbage collections. Use `UniqueRootValue`/`RootValue` rather than
 * calling this directly.
 */
Value ** allocRootValueSlot(Value * v);

/**
 * Clear the given slot and return it to the root value pool.
 */
void freeRootValueSlot(Value ** slot);

/**
 * A move-only handle rooting a Value, i.e. keeping it and everything
 * reachable from it alive across garbage collections. Prefer this
 * over `RootValue` unless the handle must be copyable (e.g. when it's
 * captured in a `std::function`-backed lambda).
 */
class UniqueRootValue
{
    Value ** slot = nullptr;

public:
    UniqueRootValue() = default;

    explicit UniqueRootValue(Value * v)
        : slot(allocRootValueSlot(v))
    {
    }

    UniqueRootValue(const UniqueRootValue &) = delete;
    UniqueRootValue & operator=(const UniqueRootValue &) = delete;

    UniqueRootValue(UniqueRootValue && other) noexcept
        : slot(std::exchange(other.slot, nullptr))
    {
    }

    UniqueRootValue & operator=(UniqueRootValue && other) noexcept
    {
        if (slot)
            freeRootValueSlot(slot);
        slot = std::exchange(other.slot, nullptr);
        return *this;
    }

    ~UniqueRootValue()
    {
        if (slot)
            freeRootValueSlot(slot);
    }

    /**
     * Release the slot, i.e. stop rooting the value.
     */
    void reset()
    {
        if (slot) {
            freeRootValueSlot(slot);
            slot = nullptr;
        }
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

/**
 * A copyable, shared handle rooting a Value. Only use this instead of
 * `UniqueRootValue` if the handle must be copyable.
 */
typedef std::shared_ptr<Value *> RootValue;

RootValue allocRootValue(Value * v);

} // namespace nix
