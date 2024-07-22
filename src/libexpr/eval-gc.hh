#pragma once
///@file

#include <cstddef>

namespace nix {

/**
 * Initialise the Boehm GC, if applicable.
 */
void initGC();

/**
 * Make sure `initGC` has already been called.
 */
void assertGCInitialized();

/**
 * The number of GC cycles since initGC().
 */
size_t getGCCycles();

}
