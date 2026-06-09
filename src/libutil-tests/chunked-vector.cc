#include "nix/util/chunked-vector.hh"

#include <gtest/gtest.h>

#include <vector>
#include <set>
#include <thread>

namespace nix {
TEST(ChunkedVector, InitEmpty)
{
    auto v = ChunkedVector<int, 2, 100>();
    ASSERT_EQ(v.size(), 0u);
}

TEST(ChunkedVector, GrowsCorrectly)
{
    auto v = ChunkedVector<int, 2, 100>();
    for (uint32_t i = 1; i < 20; i++) {
        v.add(i);
        ASSERT_EQ(v.size(), i);
    }
}

TEST(ChunkedVector, AddAndGet)
{
    auto v = ChunkedVector<int, 2, 100>();
    for (auto i = 1; i < 20; i++) {
        auto [i2, idx] = v.add(i);
        auto & i3 = v[idx];
        ASSERT_EQ(i, i2);
        ASSERT_EQ(&i2, &i3);
    }
}

TEST(ChunkedVector, ForEach)
{
    auto v = ChunkedVector<int, 2, 100>();
    for (auto i = 1; i < 20; i++) {
        v.add(i);
    }
    uint32_t count = 0;
    v.forEach([&count](int elt) { count++; });
    ASSERT_EQ(count, v.size());
}

TEST(ChunkedVector, NonTrivialType)
{
    auto v = ChunkedVector<std::vector<int>, 2, 10>();
    v.add(std::vector{1, 2});
    v.add(std::vector{3, 4});
    v.add(std::vector{5, 6});
    ASSERT_EQ(v.size(), 3);
}

TEST(ChunkedVector, ConcurrentAdd)
{
    ChunkedVector<uint64_t, /*ChunkSize=*/17, /*MaxChunks=*/8192> v;
    constexpr int nThreads = 8;
    constexpr int perThread = 8192;

    std::vector<std::thread> threads;
    std::vector<std::vector<uint32_t>> indices(nThreads);

    for (int t = 0; t < nThreads; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < perThread; ++i) {
                auto [val, idx] = v.add(static_cast<uint64_t>(t * perThread + i));
                indices[t].push_back(idx);
            }
        });
    }

    for (auto & t : threads)
        t.join();

    ASSERT_EQ(v.size(), nThreads * perThread);

    // All indices unique
    std::set<uint32_t> all;
    for (auto & vec : indices)
        for (auto idx : vec)
            ASSERT_TRUE(all.insert(idx).second);

    // All values correct
    for (int t = 0; t < nThreads; t++)
        for (int i = 0; i < perThread; i++)
            ASSERT_EQ(v[indices[t][i]], static_cast<uint64_t>(t * perThread + i));
}

} // namespace nix
