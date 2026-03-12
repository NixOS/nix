#include "nix/util/thread-pool.hh"
#include "nix/util/util.hh"
#include "nix/util/bump-memory-resource.hh"

#include <gtest/gtest.h>

#include <memory_resource>
#include <numeric>

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

} // namespace nix
