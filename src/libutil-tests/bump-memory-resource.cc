#include "nix/util/thread-pool.hh"
#include "nix/util/util.hh"
#include "nix/util/bump-memory-resource.hh"
#include "nix/util/file-system.hh"
#include "nix/util/strings.hh"
#include "nix/util/system-error.hh"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <memory_resource>
#include <numeric>

#ifdef __linux__
#  include <sys/mman.h>
#  include <unistd.h>
#endif

namespace nix {

TEST(BumpMemoryResource, ConcurrentAllocation)
{
    static constexpr unsigned numThreads = 8;
    std::pmr::synchronized_pool_resource fallbackResource;
    BumpMemoryResource resource{BumpMemoryResource::defaultReserveSize, &fallbackResource};
    std::pmr::polymorphic_allocator alloc{&resource};

    ThreadPool pool(numThreads);
    // Use std::optional to make sure our allocator is used.
    std::array<std::optional<std::pmr::vector<std::optional<std::pmr::vector<int>>>>, numThreads> vectors;

    static constexpr std::size_t intVecSize = 511;
    static constexpr std::size_t numIters = 311;

    for (auto && [i, vec] : enumerate(vectors)) {
        pool.enqueue([i, &alloc, &vec]() {
            vec = std::pmr::vector<std::optional<std::pmr::vector<int>>>(alloc);
            for (unsigned j = 0; j < numIters; ++j) {
                vec->push_back(std::pmr::vector<int>(alloc));
                auto & iotaVec = vec->back();
                iotaVec->resize(intVecSize);
                std::iota(iotaVec->begin(), iotaVec->end(), i + j);
            }
        });
    }

    pool.process();

    for (auto && [i, vec] : enumerate(vectors)) {
        ASSERT_EQ(vec->size(), numIters);
        for (const auto & [j, intVec] : enumerate(*vec))
            ASSERT_TRUE(std::ranges::equal(*intVec, std::views::iota(i + j, i + j + intVecSize)));
    }
}

#ifdef __linux__

/**
 * The `VmFlags` of the mapping that contains `p`, from `/proc/self/smaps`.
 * `hg` among them records MADV_HUGEPAGE advice, whether or not huge pages
 * back the mapping.
 */
static StringSet vmFlagsAt(const void * p)
{
    auto addr = reinterpret_cast<std::uintptr_t>(p);
    bool inMapping = false;
    for (auto & line : tokenizeString<std::vector<std::string>>(readFile("/proc/self/smaps"), "\n")) {
        if (line.starts_with("VmFlags:")) {
            if (inMapping)
                return tokenizeString<StringSet>(line.substr(8));
            continue;
        }
        char * end;
        auto start = std::strtoull(line.c_str(), &end, 16);
        if (*end != '-')
            continue;
        auto stop = std::strtoull(end + 1, nullptr, 16);
        inMapping = start <= addr && addr < stop;
    }
    throw Error("no mapping contains %p", p);
}

/**
 * Whether MADV_HUGEPAGE sets `hg` on a probe mapping. Linux without
 * transparent huge page support and qemu-user reject the advice; checking
 * the flag rather than the result also covers an implementation that
 * accepts it and does nothing.
 */
static bool hugePageAdviceRecorded()
{
#  ifdef MADV_HUGEPAGE
    auto size = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    void * p = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        throw SysError("mmap");
    ::madvise(p, size, MADV_HUGEPAGE);
    bool recorded = vmFlagsAt(p).count("hg") == 1;
    ::munmap(p, size);
    return recorded;
#  else
    return false;
#  endif
}

/* Allocating does not touch the memory, so no huge page is allocated and
   the result does not depend on memory pressure or fragmentation. */
TEST(BumpMemoryResource, HugePageAdviceBeyondPrefix)
{
    BumpMemoryResource resource;
    auto * prefix = static_cast<char *>(resource.allocate(BumpMemoryResource::regularPagesPrefix));
    auto * beyond = static_cast<char *>(resource.allocate(4096));
    if (beyond != prefix + BumpMemoryResource::regularPagesPrefix)
        GTEST_SKIP() << "no reserved region, allocations come from the upstream resource";
    EXPECT_EQ(vmFlagsAt(prefix).count("hg"), 0);
    EXPECT_EQ(vmFlagsAt(beyond - 1).count("hg"), 0);

    if (!hugePageAdviceRecorded())
        GTEST_SKIP() << "MADV_HUGEPAGE is not recorded here";
    EXPECT_EQ(vmFlagsAt(beyond).count("hg"), 1);
}

#endif

} // namespace nix
