#pragma once
///@file

#include "types.hh"

namespace nix {

/* User interruption. */

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
