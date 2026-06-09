#pragma once
///@file

#include <cstddef>

// For `NIX_USE_BOEHMGC`
#include "nix/expr/config.hh"

#if NIX_USE_BOEHMGC

#  define GC_INCLUDE_NEW
#  define GC_THREADS 1

#  include <gc/gc.h>
#  include <gc/gc_cpp.h>
#  include <gc/gc_allocator.h>

#else

#  include <memory>

/* Some dummy aliases for Boehm GC definitions to reduce the number of
   #ifdefs. */

template<typename T>
using traceable_allocator = std::allocator<T>;

template<typename T>
using gc_allocator = std::allocator<T>;

#  define GC_MALLOC_ATOMIC std::malloc

struct gc
{};

struct gc_cleanup
{};

#endif

namespace nix {

/**
 * Initialise the Boehm GC, if applicable.
 */
void initGC();

/**
 * Make sure `initGC` has already been called.
 */
void assertGCInitialized();

#if NIX_USE_BOEHMGC
/**
 * The number of GC cycles since initGC().
 */
size_t getGCCycles();
#endif

} // namespace nix
