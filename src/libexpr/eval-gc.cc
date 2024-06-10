#include "error.hh"
#include "environment-variables.hh"
#include "serialise.hh"
#include "eval-gc.hh"

#if HAVE_BOEHMGC

#  include <pthread.h>
#  if __FreeBSD__
#    include <pthread_np.h>
#  endif

#  include <gc/gc.h>
#  include <gc/gc_cpp.h>
#  include <gc/gc_allocator.h>

#  include <boost/coroutine2/coroutine.hpp>
#  include <boost/coroutine2/protected_fixedsize_stack.hpp>
#  include <boost/context/stack_context.hpp>

#endif

namespace nix {

#if HAVE_BOEHMGC
/* Called when the Boehm GC runs out of memory. */
static void * oomHandler(size_t requested)
{
    /* Convert this to a proper C++ exception. */
    throw std::bad_alloc();
}

class BoehmGCStackAllocator : public StackAllocator
{
    boost::coroutines2::protected_fixedsize_stack stack{
        // We allocate 8 MB, the default max stack size on NixOS.
        // A smaller stack might be quicker to allocate but reduces the stack
        // depth available for source filter expressions etc.
        std::max(boost::context::stack_traits::default_size(), static_cast<std::size_t>(8 * 1024 * 1024))};

    // This is specific to boost::coroutines2::protected_fixedsize_stack.
    // The stack protection page is included in sctx.size, so we have to
    // subtract one page size from the stack size.
    std::size_t pfss_usable_stack_size(boost::context::stack_context & sctx)
    {
        return sctx.size - boost::context::stack_traits::page_size();
    }

public:
    boost::context::stack_context allocate() override
    {
        auto sctx = stack.allocate();

        // Stacks generally start at a high address and grow to lower addresses.
        // Architectures that do the opposite are rare; in fact so rare that
        // boost_routine does not implement it.
        // So we subtract the stack size.
        GC_add_roots(static_cast<char *>(sctx.sp) - pfss_usable_stack_size(sctx), sctx.sp);
        return sctx;
    }

    void deallocate(boost::context::stack_context sctx) override
    {
        GC_remove_roots(static_cast<char *>(sctx.sp) - pfss_usable_stack_size(sctx), sctx.sp);
        stack.deallocate(sctx);
    }
};

static BoehmGCStackAllocator boehmGCStackAllocator;

/**
 * When a thread goes into a coroutine, we lose its original sp until
 * control flow returns to the thread.
 * While in the coroutine, the sp points outside the thread stack,
 * so we can detect this and push the entire thread stack instead,
 * as an approximation.
 * The coroutine's stack is covered by `BoehmGCStackAllocator`.
 * This is not an optimal solution, because the garbage is scanned when a
 * coroutine is active, for both the coroutine and the original thread stack.
 * However, the implementation is quite lean, and usually we don't have active
 * coroutines during evaluation, so this is acceptable.
 */
void fixupBoehmStackPointer(void ** sp_ptr, void * _pthread_id)
{
    void *& sp = *sp_ptr;
    auto pthread_id = reinterpret_cast<pthread_t>(_pthread_id);
    pthread_attr_t pattr;
    size_t osStackSize;
    void * osStackLow;
    void * osStackBase;

#  ifdef __APPLE__
    osStackSize = pthread_get_stacksize_np(pthread_id);
    osStackLow = pthread_get_stackaddr_np(pthread_id);
#  else
    if (pthread_attr_init(&pattr)) {
        throw Error("fixupBoehmStackPointer: pthread_attr_init failed");
    }
#    ifdef HAVE_PTHREAD_GETATTR_NP
    if (pthread_getattr_np(pthread_id, &pattr)) {
        throw Error("fixupBoehmStackPointer: pthread_getattr_np failed");
    }
#    elif HAVE_PTHREAD_ATTR_GET_NP
    if (!pthread_attr_init(&pattr)) {
        throw Error("fixupBoehmStackPointer: pthread_attr_init failed");
    }
    if (!pthread_attr_get_np(pthread_id, &pattr)) {
        throw Error("fixupBoehmStackPointer: pthread_attr_get_np failed");
    }
#    else
#      error "Need one of `pthread_attr_get_np` or `pthread_getattr_np`"
#    endif
    if (pthread_attr_getstack(&pattr, &osStackLow, &osStackSize)) {
        throw Error("fixupBoehmStackPointer: pthread_attr_getstack failed");
    }
    if (pthread_attr_destroy(&pattr)) {
        throw Error("fixupBoehmStackPointer: pthread_attr_destroy failed");
    }
#  endif
    osStackBase = (char *) osStackLow + osStackSize;
    // NOTE: We assume the stack grows down, as it does on all architectures we support.
    //       Architectures that grow the stack up are rare.
    if (sp >= osStackBase || sp < osStackLow) { // lo is outside the os stack
        sp = osStackBase;
    }
}

/* Disable GC while this object lives. Used by CoroutineContext.
 *
 * Boehm keeps a count of GC_disable() and GC_enable() calls,
 * and only enables GC when the count matches.
 */
class BoehmDisableGC
{
public:
    BoehmDisableGC()
    {
        GC_disable();
    };
    ~BoehmDisableGC()
    {
        GC_enable();
    };
};

static inline void initGCReal()
{
    /* Make sure we're the first to initialize the GC. If we're not, GC_init()
       will exit early, and therefore fail to assign values from environment
       variables we're about to modify.
       Some setters should also be called before GC_init(). */
    if (GC_is_init_called()) {
        /* This can be a problem in the nix command, or perhaps a problem for someone
           who's linked against libnixexpr. */
        warn(
            "to developers: GC_init() has already been called. Some parameters may not be set optimally or correctly. Make sure to always call nix::initGC() before using the GC.");
    }

    /* Initialise the Boehm garbage collector. */

    /* Don't look for interior pointers. This reduces the odds of
       misdetection a bit. */
    GC_set_all_interior_pointers(0);

    /* We don't have any roots in data segments, so don't scan from
       there. */
    GC_set_no_dls(1);

    /* The GC threshold determines the number of collections during which an
       unmappable block is kept alive. Empirically[1] we have a lot of memory
       that can be unmapped towards the end of evaluation, and we don't perform
       many allocations after that.
       0 is a special value that disables unmapping altogether, so we set this
       to 1. This also seems sensible to avoid the overhead of unmapping and
       soon remapping the ~same memory.
       However, in an upcoming (as of 2024-06) version, the semantics will change,
       so that a value of 0 means "unmap immediately"[2].
       At that point, we should probably set this to 2, to avoid aforementioned
       overhead.
       When implementing a final collection after e.g. `nix-build` has evaluated,
       we should make sure to call GC_gcollect_and_unmap, which has an effect
       similar to setting this to 1.
       As of 2024-06, GC_unmap_threshold is only assignable through GC_init(),
       so we have to do a little dance with environment variables.

       [1]: https://github.com/NixOS/nix/issues/10862#issuecomment-2154463760
       [2]: https://github.com/ivmai/bdwgc/commit/ece628e5a347dd319cffe4b53dbd73ebc42d0b4d
     */
    auto origUnmapThreshold = getEnv("GC_UNMAP_THRESHOLD");
    if (!origUnmapThreshold) {
        setEnv("GC_UNMAP_THRESHOLD", "1");
    }

    GC_INIT();

    /* Restore environment variables. We host commands like nix run, which must
       not modify the environment more than necessary. */
    setMaybeEnv("GC_UNMAP_THRESHOLD", origUnmapThreshold);

    GC_set_oom_fn(oomHandler);

    StackAllocator::defaultAllocator = &boehmGCStackAllocator;

// TODO: Remove __APPLE__ condition.
//       Comment suggests an implementation that works on darwin and windows
//       https://github.com/ivmai/bdwgc/issues/362#issuecomment-1936672196
#  if GC_VERSION_MAJOR >= 8 && GC_VERSION_MINOR >= 2 && GC_VERSION_MICRO >= 4 && !defined(__APPLE__)
    GC_set_sp_corrector(&fixupBoehmStackPointer);

    if (!GC_get_sp_corrector()) {
        printTalkative("BoehmGC on this platform does not support sp_corrector; will disable GC inside coroutines");
        /* Used to disable GC when entering coroutines on macOS */
        create_coro_gc_hook = []() -> std::shared_ptr<void> { return std::make_shared<BoehmDisableGC>(); };
    }
#  else
#    warning \
        "BoehmGC version does not support GC while coroutine exists. GC will be disabled inside coroutines. Consider updating bdw-gc to 8.2.4 or later."
#  endif

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

#endif

static bool gcInitialised = false;

void initGC()
{
    if (gcInitialised)
        return;

#if HAVE_BOEHMGC
    initGCReal();
#endif

    gcInitialised = true;
}

void assertGCInitialized()
{
    assert(gcInitialised);
}

}
