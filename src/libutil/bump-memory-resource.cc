#include "nix/util/bump-memory-resource.hh"
#include "nix/util/file-system.hh"
#include "nix/util/alignment.hh"
#include "nix/util/logging.hh"

#ifndef _WIN32
#  include <charconv>

#  include <sys/resource.h>
#  include <sys/mman.h>
#endif

namespace nix {

#ifndef _WIN32

#  ifdef __linux__
static bool canOvercommitLinux()
try {
    auto policyS = readFile("/proc/sys/vm/overcommit_memory");
    int policy = -1;
    auto res = std::from_chars(policyS.data(), policyS.data() + policyS.size(), policy, /*base=*/10);
    if (res.ec != std::errc()) /* Got garbage somehow. */
        return false;
    return policy != 2;
} catch (SystemError & e) {
    debug("could not read /proc/sys/vm/overcommit_memory: %s", e.message());
    return false;
}

#  endif

static bool checkRlimit(std::size_t reserveSize)
{
    struct rlimit rl;
    if (::getrlimit(RLIMIT_AS, &rl) != 0)
        return true;
    if (rl.rlim_cur == RLIM_INFINITY)
        return true;
    return reserveSize <= static_cast<std::size_t>(rl.rlim_cur) / 16; /* Have some headroom too. */
}

static bool canOvercommit(std::size_t reserveSize)
{
    /* Don't bother with 32 bit address space. */
    if constexpr (sizeof(void *) < 8)
        return false;

#  ifdef __linux__
    static bool overcommitPossible = canOvercommitLinux();
    if (!overcommitPossible)
        return false;
#  endif

    return checkRlimit(reserveSize);
}

#endif // _WIN32

BumpMemoryResource::BumpMemoryResource(std::size_t reserveSize, std::pmr::memory_resource * upstream)
    : upstreamResource(upstream)
{
#ifndef _WIN32
    if (!canOvercommit(reserveSize))
        return;

    int flags = MAP_PRIVATE;

#  ifdef MAP_ANON /* macOS might not have MAP_ANONYMOUS */
    flags |= MAP_ANON;
#  else
    flags |= MAP_ANONYMOUS;
#  endif

#  ifdef MAP_NORESERVE
    flags |= MAP_NORESERVE;
#  endif

    void * p = ::mmap(/*addr=*/nullptr, reserveSize, PROT_READ | PROT_WRITE, flags, /*fd=*/-1, /*offset=*/0);
    if (p == MAP_FAILED)
        /* Will fall back to the upstream resource. */
        return;

    base = p;
    capacity = reserveSize;
#endif
}

BumpMemoryResource::~BumpMemoryResource()
{
#ifndef _WIN32
    if (base)
        ::munmap(base, capacity);
#endif
}

void * BumpMemoryResource::do_allocate(std::size_t bytes, std::size_t alignment)
{
    if (!base)
        return upstreamResource->allocate(bytes, alignment);

    auto current = offset.load(std::memory_order_relaxed);
    for (;;) { /* CAS loop to bump the offset pointer. */
        auto aligned = alignUp(current, alignment);
        auto next = aligned + bytes;
        if (next > capacity) /* Fall back to the upstream resource. */
            return upstreamResource->allocate(bytes, alignment);

        /* Try to bump the pointer. */
        if (offset.compare_exchange_weak(current, next, std::memory_order_relaxed))
            return static_cast<char *>(base) + aligned;
    }
}

void BumpMemoryResource::do_deallocate(void * p, std::size_t bytes, std::size_t alignment)
{
    /* Does nothing. Deallocate at destruction time. Upstream resource should also deallocate
       at destruction time. */
}

} // namespace nix
