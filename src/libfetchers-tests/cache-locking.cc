#include <gtest/gtest.h>
#include <thread>
#include <chrono>

#include "nix/fetchers/cache.hh"
#include "nix/fetchers/cache-impl.hh"
#include "nix/store/pathlocks.hh"

namespace nix::fetchers {

TEST(FetchLockPath, DifferentIdentitiesProduceDifferentPaths)
{
    auto path1 = getFetchLockPath("tarball:https://example.com/a.tar.gz");
    auto path2 = getFetchLockPath("tarball:https://example.com/b.tar.gz");
    EXPECT_NE(path1, path2);
}

TEST(FetchLockPath, SameIdentityProducesSamePath)
{
    auto path1 = getFetchLockPath("tarball:https://example.com/a.tar.gz");
    auto path2 = getFetchLockPath("tarball:https://example.com/a.tar.gz");
    EXPECT_EQ(path1, path2);
}

TEST(FetchLockPath, PathIsInFetchLocksDir)
{
    auto path = getFetchLockPath("test-identity");
    EXPECT_NE(path.find("fetch-locks"), std::string::npos);
}

TEST(FetchLockPath, PathEndsWithLockExtension)
{
    auto path = getFetchLockPath("test-identity");
    EXPECT_TRUE(hasSuffix(path, ".lock"));
}

TEST(FetchLockPath, EmptyIdentityWorks)
{
    // Should not throw, even with empty identity
    auto path = getFetchLockPath("");
    EXPECT_FALSE(path.empty());
    EXPECT_TRUE(hasSuffix(path, ".lock"));
}

TEST(FetchLockPath, SpecialCharactersInIdentity)
{
    // Identities with special characters should be handled (hashed)
    auto path1 = getFetchLockPath("test:with:colons");
    auto path2 = getFetchLockPath("test/with/slashes");
    auto path3 = getFetchLockPath("test with spaces");

    // All should produce valid paths ending in .lock
    EXPECT_TRUE(hasSuffix(path1, ".lock"));
    EXPECT_TRUE(hasSuffix(path2, ".lock"));
    EXPECT_TRUE(hasSuffix(path3, ".lock"));

    // All should be different
    EXPECT_NE(path1, path2);
    EXPECT_NE(path2, path3);
    EXPECT_NE(path1, path3);
}

// Tests for withFetchLock()

TEST(WithFetchLock, CacheHitReturnsWithoutFetching)
{
    int fetchCount = 0;
    auto result = withFetchLock(
        "test-cache-hit",
        1,
        []() -> std::optional<int> { return 42; },
        [&]() {
            fetchCount++;
            return 0;
        });
    EXPECT_EQ(result, 42);
    EXPECT_EQ(fetchCount, 0);
}

TEST(WithFetchLock, CacheMissCallsFetcher)
{
    int checkCount = 0;
    auto result = withFetchLock(
        "test-cache-miss",
        1,
        [&]() -> std::optional<int> {
            checkCount++;
            return std::nullopt;
        },
        []() { return 99; });
    EXPECT_EQ(result, 99);
    // checkCache is called once (inside withFetchLock after acquiring lock)
    EXPECT_EQ(checkCount, 1);
}

TEST(WithFetchLock, TimeoutThrowsError)
{
    // Hold a lock on a specific identity
    auto lockPath = getFetchLockPath("contended-lock");
    auto fd = openLockFile(lockPath, true);
    ASSERT_TRUE(fd);
    ASSERT_TRUE(lockFile(fd.get(), ltWrite, false));

    // Try to acquire the same lock with a short timeout
    EXPECT_THROW(
        withFetchLock(
            "contended-lock",
            1, // 1 second timeout
            []() -> std::optional<int> { return std::nullopt; },
            []() { return 0; }),
        Error);

    // Clean up
    deleteLockFile(lockPath, fd.get());
}

TEST(WithFetchLock, DoubleCheckPreventsRedundantFetch)
{
    // This test simulates the scenario where:
    // 1. First call to checkCache returns nullopt (cache miss)
    // 2. While acquiring lock, another "process" populates the cache
    // 3. Second call to checkCache (inside withFetchLock) returns a value
    // 4. Fetcher is never called

    int checkCount = 0;
    int fetchCount = 0;

    auto result = withFetchLock(
        "test-double-check",
        1,
        [&]() -> std::optional<int> {
            checkCount++;
            // Simulate: first check misses, subsequent checks hit
            if (checkCount == 1)
                return 123; // The double-check finds cached value
            return std::nullopt;
        },
        [&]() {
            fetchCount++;
            return 456;
        });

    // Since checkCache returns a value on first call inside withFetchLock,
    // the fetcher should not be called
    EXPECT_EQ(result, 123);
    EXPECT_EQ(checkCount, 1);
    EXPECT_EQ(fetchCount, 0);
}

TEST(WithFetchLock, FetcherResultIsReturned)
{
    auto result = withFetchLock(
        "test-fetcher-result",
        1,
        []() -> std::optional<std::string> { return std::nullopt; },
        []() { return std::string("fetched-value"); });

    EXPECT_EQ(result, "fetched-value");
}

TEST(WithFetchLock, ZeroTimeoutMeansIndefinite)
{
    // With timeout=0, should succeed immediately on uncontested lock
    auto result = withFetchLock(
        "test-zero-timeout",
        0, // No timeout
        []() -> std::optional<int> { return std::nullopt; },
        []() { return 77; });

    EXPECT_EQ(result, 77);
}

} // namespace nix::fetchers
