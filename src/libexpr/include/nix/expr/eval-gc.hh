#pragma once
///@file

#include <cstddef>
#include <cstring>

// For `NIX_USE_BOEHMGC`
#include "nix/expr/config.hh"

#if NIX_USE_BOEHMGC

#  define GC_INCLUDE_NEW
#  define GC_THREADS 1

#  include <gc/gc.h>
#  include <gc/gc_cpp.h>
#  include <gc/gc_allocator.h>

/**
 * A `traceable_allocator` that zeroes destroyed elements.
 *
 * Boehm scans the backing storage of containers using `traceable_allocator`,
 * including slots whose elements have been erased. Open-addressing containers
 * such as `boost::concurrent_flat_map` keep the element array around after
 * `erase()` or `clear()`, so the stale pointers left in those slots keep
 * unreachable objects alive. Zeroing the storage on destruction avoids that.
 */
template<typename T>
struct gc_root_allocator : traceable_allocator<T>
{
    using traceable_allocator<T>::traceable_allocator;

    template<typename U>
    struct rebind
    {
        using other = gc_root_allocator<U>;
    };

    template<typename U>
    void destroy(U * p)
    {
        p->~U();
        std::memset(static_cast<void *>(p), 0, sizeof(U));
    }
};

#else

#  include <memory>

/* Some dummy aliases for Boehm GC definitions to reduce the number of
   #ifdefs. */

template<typename T>
using traceable_allocator = std::allocator<T>;

template<typename T>
using gc_allocator = std::allocator<T>;

template<typename T>
using gc_root_allocator = std::allocator<T>;

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
