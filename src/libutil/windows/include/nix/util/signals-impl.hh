#pragma once
///@file

#include "nix/util/types.hh"
#include "nix/util/fun.hh"

#include <memory>

namespace nix {

class InterruptCallback;

/* User interruption. */

static inline void setInterrupted(bool isInterrupted)
{
    /* Do nothing for now */
}

static inline bool getInterrupted()
{
    return false;
}

static inline bool isInterrupted()
{
    /* Do nothing for now */
    return false;
}

inline void checkInterrupt()
{
    /* Do nothing for now */
}

/**
 * Does nothing, unlike Unix counterpart, but allows avoiding C++
 */
struct ReceiveInterrupts
{
    /**
     * Explicit destructor avoids dead code warnings.
     */
    ~ReceiveInterrupts() {}
};

inline std::unique_ptr<InterruptCallback> createInterruptCallback(fun<void()> callback)
{
    return {};
}

} // namespace nix
