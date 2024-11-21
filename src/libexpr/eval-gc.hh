#pragma once
///@file

#include <cstddef>

#if HAVE_BOEHMGC

#  define GC_INCLUDE_NEW

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

#ifdef HAVE_BOEHMGC
/**
 * The number of GC cycles since initGC().
 */
size_t getGCCycles();
#endif

} // namespace nix
