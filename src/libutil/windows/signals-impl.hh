#pragma once
///@file

#include "types.hh"

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

void inline checkInterrupt()
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

}
