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

#  include <boost/coroutine2/coroutine.hpp>
#  include <boost/coroutine2/protected_fixedsize_stack.hpp>
#  include <boost/context/stack_context.hpp>

#endif

namespace nix {

#if NIX_USE_BOEHMGC
/* Called when the Boehm GC runs out of memory. */
static void * oomHandler(size_t requested)
{
    /* Convert this to a proper C++ exception. */
    throw std::bad_alloc();
}

static size_t getFreeMem()
{
    /* On Linux, use the `MemAvailable` or `MemFree` fields from
       /proc/cpuinfo. */
#  ifdef __linux__
    {
        std::unordered_map<std::string, std::string> fields;
        for (auto & line :
             tokenizeString<std::vector<std::string>>(readFile(std::filesystem::path("/proc/meminfo")), "\n")) {
            auto colon = line.find(':');
            if (colon == line.npos)
                continue;
            fields.emplace(line.substr(0, colon), trim(line.substr(colon + 1)));
        }

        auto i = fields.find("MemAvailable");
        if (i == fields.end())
            i = fields.find("MemFree");
        if (i != fields.end()) {
            auto kb = tokenizeString<std::vector<std::string>>(i->second, " ");
            if (kb.size() == 2 && kb[1] == "kB")
                return string2Int<size_t>(kb[0]).value_or(0) * 1024;
        }
    }
#  endif

    /* On non-Linux systems, conservatively assume that 25% of memory is free. */
    long pageSize = sysconf(_SC_PAGESIZE);
    long pages = sysconf(_SC_PHYS_PAGES);
    if (pageSize != -1)
        return (pageSize * pages) / 4;
    return 0;
}

/**
 * When a thread goes into a coroutine, we lose its original sp until
 * control flow returns to the thread. This causes Boehm GC to crash
 * since it will scan memory between the coroutine's sp and the
 * original stack base of the thread. Therefore, we detect when the
 * current sp is outside of the original thread stack and push the
 * entire thread stack instead, as an approximation.
 *
 * This is not optimal, because it causes the stack below sp to be
 * scanned. However, we usually we don't have active coroutines during
 * evaluation, so this is acceptable.
 *
 * Note that we don't scan coroutine stacks. It's currently assumed
 * that we don't have GC roots in coroutines.
 */
void fixupBoehmStackPointer(void ** sp_ptr, void * _pthread_id)
{
    void *& sp = *sp_ptr;
    auto pthread_id = reinterpret_cast<pthread_t>(_pthread_id);
    size_t osStackSize;
    char * osStackHi;
    char * osStackLo;

#  ifdef __APPLE__
    osStackSize = pthread_get_stacksize_np(pthread_id);
    osStackHi = (char *) pthread_get_stackaddr_np(pthread_id);
    osStackLo = osStackHi - osStackSize;
#  else
    pthread_attr_t pattr;
    if (pthread_attr_init(&pattr))
        throw Error("fixupBoehmStackPointer: pthread_attr_init failed");
#    ifdef HAVE_PTHREAD_GETATTR_NP
    if (pthread_getattr_np(pthread_id, &pattr))
        throw Error("fixupBoehmStackPointer: pthread_getattr_np failed");
#    else
#      error "Need  `pthread_attr_get_np`"
#    endif
    if (pthread_attr_getstack(&pattr, (void **) &osStackLo, &osStackSize))
        throw Error("fixupBoehmStackPointer: pthread_attr_getstack failed");
    if (pthread_attr_destroy(&pattr))
        throw Error("fixupBoehmStackPointer: pthread_attr_destroy failed");
    osStackHi = osStackLo + osStackSize;
#  endif

    if (sp >= osStackHi || sp < osStackLo) // sp is outside the os stack
        sp = osStackLo;
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

    GC_set_sp_corrector(&fixupBoehmStackPointer);
    assert(GC_get_sp_corrector());

    /* Set the initial heap size to something fairly big (80% of
       free RAM, up to a maximum of 4 GiB) so that in most cases
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
        size_t maxSize = 4ULL * 1024 * 1024 * 1024;
        auto free = getFreeMem();
        size = std::max(size, std::min((size_t) (free * 0.5), maxSize));
#  endif
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
