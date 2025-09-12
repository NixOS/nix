#include "nix/util/error.hh"
#include "nix/util/environment-variables.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/util/config-global.hh"
#include "nix/util/serialise.hh"
#include "nix/expr/eval-gc.hh"
#include "nix/expr/value.hh"

#include "expr-config-private.hh"

#if NIX_USE_BOEHMGC

#  include <pthread.h>
#  ifdef __FreeBSD__
#    include <pthread_np.h>
#  endif

#  include <gc/gc_allocator.h>
#  include <gc/gc_tiny_fl.h> // For GC_GRANULE_BYTES

#  include <boost/coroutine2/coroutine.hpp>
#  include <boost/coroutine2/protected_fixedsize_stack.hpp>
#  include <boost/context/stack_context.hpp>

#endif

namespace nix {

#if NIX_USE_BOEHMGC

/*
 * Ensure that Boehm satisfies our alignment requirements. This is the default configuration [^]
 * and this assertion should never break for any platform. Let's assert it just in case.
 *
 * This alignment is particularly useful to be able to use aligned
 * load/store instructions for loading/writing Values.
 *
 * [^]: https://github.com/bdwgc/bdwgc/blob/54ac18ccbc5a833dd7edaff94a10ab9b65044d61/include/gc/gc_tiny_fl.h#L31-L33
 */
static_assert(sizeof(void *) * 2 == GC_GRANULE_BYTES, "Boehm GC must use GC_GRANULE_WORDS = 2");

/* Called when the Boehm GC runs out of memory. */
static void * oomHandler(size_t requested)
{
    /* Convert this to a proper C++ exception. */
    throw std::bad_alloc();
}

static inline void initGCReal()
{
    /* Initialise the Boehm garbage collector. */

    /* Don't look for interior pointers. This reduces the odds of
       misdetection a bit. */
    GC_set_all_interior_pointers(0);

    /* We don't have any roots in data segments, so don't scan from
       there. */
    GC_set_no_dls(1);

    /* Enable perf measurements. This is just a setting; not much of a
       start of something. */
    GC_start_performance_measurement();

    GC_INIT();

    /* Enable parallel marking. */
    GC_allow_register_threads();

    /* Register valid displacements in case we are using alignment niches
       for storing the type information. This way tagged pointers are considered
       to be valid, even when they are not aligned. */
    if constexpr (detail::useBitPackedValueStorage<sizeof(void *)>)
        for (std::size_t i = 1; i < sizeof(std::uintptr_t); ++i)
            GC_register_displacement(i);

    GC_set_oom_fn(oomHandler);

    /* Set the initial heap size to something fairly big (25% of
       physical RAM, up to a maximum of 384 MiB) so that in most cases
       we don't need to garbage collect at all.  (Collection has a
       fairly significant overhead.)  The heap size can be overridden
       through libgc's GC_INITIAL_HEAP_SIZE environment variable.  We
       should probably also provide a nix.conf setting for this.  Note
       that GC_expand_hp() causes a lot of virtual, but not physical
       (resident) memory to be allocated.  This might be a problem on
       systems that don't overcommit. */
    if (!getEnv("GC_INITIAL_HEAP_SIZE")) {
        size_t size = 32 * 1024 * 1024;
#  if HAVE_SYSCONF && defined(_SC_PAGESIZE) && defined(_SC_PHYS_PAGES)
        size_t maxSize = 384 * 1024 * 1024;
        long pageSize = sysconf(_SC_PAGESIZE);
        long pages = sysconf(_SC_PHYS_PAGES);
        if (pageSize != -1)
            size = (pageSize * pages) / 4; // 25% of RAM
        if (size > maxSize)
            size = maxSize;
#  endif
        debug("setting initial heap size to %1% bytes", size);
        GC_expand_hp(size);
    }
}

static size_t gcCyclesAfterInit = 0;

size_t getGCCycles()
{
    assertGCInitialized();
    return static_cast<size_t>(GC_get_gc_no()) - gcCyclesAfterInit;
}

#endif

static bool gcInitialised = false;

void initGC()
{
    if (gcInitialised)
        return;

#if NIX_USE_BOEHMGC
    initGCReal();

    gcCyclesAfterInit = GC_get_gc_no();
#endif

    // NIX_PATH must override the regular setting
    // See the comment in applyConfig
    if (auto nixPathEnv = getEnv("NIX_PATH")) {
        globalConfig.set("nix-path", concatStringsSep(" ", EvalSettings::parseNixPath(nixPathEnv.value())));
    }

    gcInitialised = true;
}

void assertGCInitialized()
{
    assert(gcInitialised);
}

} // namespace nix
