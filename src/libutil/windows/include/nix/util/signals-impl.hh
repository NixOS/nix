#pragma once
///@file

#include "nix/util/types.hh"

namespace nix {

/* User interruption. */

static inline void setInterrupted(bool isInterrupted)
{
    /* Do nothing for now */
}

static inline bool getInterrupted()
{
    return false;
}

inline void setInterruptThrown()
{
    /* Do nothing for now */
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

} // namespace nix
