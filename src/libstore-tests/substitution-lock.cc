#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <sys/stat.h>

#ifndef _WIN32
#  include <sys/wait.h>
#  include <unistd.h>
#  include <signal.h>
#endif

#include "nix/store/substitution-lock.hh"
#include "nix/store/substitution-lock-impl.hh"
#include "nix/util/file-system.hh"

namespace nix {

class SubstitutionLockTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Tests use the real cache directory for lock files
    }
};

// Basic functionality tests

TEST_F(SubstitutionLockTest, LockPathIsUnique)
{
    auto path1 = getSubstitutionLockPath("abc123");
    auto path2 = getSubstitutionLockPath("def456");
    auto path3 = getSubstitutionLockPath("abc123");

    // Different hashes should produce different lock paths
    EXPECT_NE(path1, path2);
    // Same hash should produce same lock path
    EXPECT_EQ(path1, path3);
}

TEST_F(SubstitutionLockTest, LockPathContainsHashPart)
{
    auto path = getSubstitutionLockPath("abc123xyz");
    // Lock path should contain the hash part
    EXPECT_NE(path.find("abc123xyz"), std::string::npos);
    // Lock path should have .lock extension
    EXPECT_NE(path.find(".lock"), std::string::npos);
}

TEST_F(SubstitutionLockTest, LockPathInCacheDir)
{
    auto path = getSubstitutionLockPath("test123");
    // Lock path should be in the substitution-locks directory
    EXPECT_NE(path.find("substitution-locks"), std::string::npos);
}

TEST_F(SubstitutionLockTest, CacheHitSkipsCopy)
{
    bool copyExecuted = false;

    withSubstitutionLock(
        "test-cache-hit",
        1,
        [&]() -> bool {
            return true; // Simulate cache hit
        },
        [&]() { copyExecuted = true; });

    EXPECT_FALSE(copyExecuted);
}

TEST_F(SubstitutionLockTest, CacheMissExecutesCopy)
{
    bool copyExecuted = false;

    withSubstitutionLock(
        "test-cache-miss",
        1,
        [&]() -> bool {
            return false; // Simulate cache miss
        },
        [&]() { copyExecuted = true; });

    EXPECT_TRUE(copyExecuted);
}

TEST_F(SubstitutionLockTest, DoubleCheckPreventsRedundantCopy)
{
    // This test simulates the scenario where another process completed the
    // substitution while we were waiting for the lock. The checkExists callback
    // (called after acquiring the lock) returns true, so we skip the copy.
    int copyCount = 0;

    withSubstitutionLock(
        "test-double-check",
        1,
        [&]() -> bool {
            // Simulate: another process completed substitution while we waited
            return true;
        },
        [&]() { copyCount++; });

    // Copy should not execute because checkExists returned true
    EXPECT_EQ(copyCount, 0);
}

TEST_F(SubstitutionLockTest, ExceptionFromDoCopyPropagates)
{
    // Verify that exceptions from doCopy propagate correctly and the lock is released
    bool exceptionCaught = false;

    try {
        withSubstitutionLock(
            "test-exception",
            1,
            [&]() -> bool { return false; },
            [&]() { throw std::runtime_error("test error from doCopy"); });
    } catch (const std::runtime_error & e) {
        exceptionCaught = true;
        EXPECT_STREQ(e.what(), "test error from doCopy");
    }

    EXPECT_TRUE(exceptionCaught);

    // Verify lock was released by acquiring it again
    bool secondLockAcquired = false;
    withSubstitutionLock("test-exception", 1, [&]() -> bool { return false; }, [&]() { secondLockAcquired = true; });

    EXPECT_TRUE(secondLockAcquired);
}

// Thread-based contention tests

TEST_F(SubstitutionLockTest, ConcurrentLocksSerialize)
{
    std::atomic<int> activeCount{0};
    std::atomic<int> maxConcurrent{0};
    std::atomic<int> completedCount{0};

    auto worker = [&](const std::string & id) {
        withSubstitutionLock(
            "concurrent-test",
            10,
            [&]() -> bool { return false; },
            [&]() {
                int current = ++activeCount;
                int expected = maxConcurrent.load();
                while (current > expected && !maxConcurrent.compare_exchange_weak(expected, current)) {
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                --activeCount;
                ++completedCount;
            });
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 3; i++) {
        threads.emplace_back(worker, std::to_string(i));
    }

    for (auto & t : threads) {
        t.join();
    }

    // All workers should have completed
    EXPECT_EQ(completedCount.load(), 3);
    // Due to locking, max concurrent should be 1
    EXPECT_EQ(maxConcurrent.load(), 1);
}

// Process-based contention tests (Unix only - uses fork/waitpid)

#ifndef _WIN32

TEST_F(SubstitutionLockTest, ProcessContention_SecondProcessWaits)
{
    int syncPipe[2];
    ASSERT_EQ(pipe(syncPipe), 0);

    pid_t pid = fork();
    if (pid == 0) {
        close(syncPipe[0]);
        // Child: acquire lock, hold for 200ms, then exit
        withSubstitutionLock(
            "process-contention-test",
            5,
            [&]() -> bool {
                char ready = '1';
                (void) !write(syncPipe[1], &ready, 1);
                return false;
            },
            [&]() { std::this_thread::sleep_for(std::chrono::milliseconds(200)); });
        close(syncPipe[1]);
        _exit(0);
    }

    close(syncPipe[1]);
    char ready = 0;
    ASSERT_EQ(read(syncPipe[0], &ready, 1), 1);
    close(syncPipe[0]);

    auto start = std::chrono::steady_clock::now();
    bool copyExecuted = false;

    withSubstitutionLock("process-contention-test", 5, [&]() -> bool { return false; }, [&]() { copyExecuted = true; });

    auto elapsed = std::chrono::steady_clock::now() - start;

    // Should have waited for child to release lock
    EXPECT_GE(elapsed, std::chrono::milliseconds(100));
    EXPECT_TRUE(copyExecuted);

    int status;
    waitpid(pid, &status, 0);
}

TEST_F(SubstitutionLockTest, ProcessContention_TimeoutThrows)
{
    int syncPipe[2];
    ASSERT_EQ(pipe(syncPipe), 0);

    pid_t pid = fork();
    if (pid == 0) {
        close(syncPipe[0]);
        // Child: acquire lock and hold for 5 seconds (longer than parent's timeout)
        withSubstitutionLock(
            "process-timeout-test",
            0, // No timeout for child
            [&]() -> bool {
                char ready = '1';
                (void) !write(syncPipe[1], &ready, 1);
                return false;
            },
            [&]() { std::this_thread::sleep_for(std::chrono::seconds(5)); });
        close(syncPipe[1]);
        _exit(0);
    }

    close(syncPipe[1]);
    char ready = 0;
    ASSERT_EQ(read(syncPipe[0], &ready, 1), 1);
    close(syncPipe[0]);

    bool threwException = false;
    auto start = std::chrono::steady_clock::now();

    try {
        withSubstitutionLock(
            "process-timeout-test",
            1,
            [&]() -> bool { return false; },
            [&]() {
                // Should not execute due to timeout
            });
    } catch (const Error &) {
        threwException = true;
    }

    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(threwException);
    EXPECT_GE(elapsed, std::chrono::milliseconds(900));
    EXPECT_LE(elapsed, std::chrono::milliseconds(1500));

    // Clean up child
    kill(pid, SIGTERM);
    int status;
    waitpid(pid, &status, 0);
}

TEST_F(SubstitutionLockTest, ProcessCrash_LockReleased)
{
    pid_t pid = fork();
    if (pid == 0) {
        // Child: acquire lock then crash
        auto lockPath = getSubstitutionLockPath("process-crash-test");
        auto fd = openLockFile(lockPath, true);
        lockFile(fd.get(), ltWrite, false);
        _exit(1); // Simulate crash
    }

    // Wait for child to exit
    int status;
    waitpid(pid, &status, 0);

    // Parent should be able to acquire lock immediately
    bool copyExecuted = false;

    withSubstitutionLock("process-crash-test", 1, [&]() -> bool { return false; }, [&]() { copyExecuted = true; });

    EXPECT_TRUE(copyExecuted);
}

// ============================================================================
// Stale Lock Detection Tests (Task 3)
// ============================================================================

TEST_F(SubstitutionLockTest, StaleLock_DetectsUnlinkedFile)
{
    // Test that lock acquisition detects when the lock file has been unlinked
    // (st_nlink == 0) and retries with a new file.
    auto lockPath = getSubstitutionLockPath("stale-nlink-test");

    int syncPipe[2];
    ASSERT_EQ(pipe(syncPipe), 0);

    pid_t pid = fork();
    if (pid == 0) {
        close(syncPipe[0]);
        // Child: acquire lock, unlink file (simulating deleteLockFile),
        // then hold the fd open for a bit
        auto fd = openLockFile(lockPath, true);
        lockFile(fd.get(), ltWrite, false);
        unlink(lockPath.c_str()); // Unlink but keep fd open
        char ready = '1';
        (void) !write(syncPipe[1], &ready, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        close(syncPipe[1]);
        _exit(0);
    }

    close(syncPipe[1]);
    char ready = 0;
    ASSERT_EQ(read(syncPipe[0], &ready, 1), 1);
    close(syncPipe[0]);

    // Should be able to acquire lock (creates new file)
    bool copyExecuted = false;
    withSubstitutionLock("stale-nlink-test", 2, [&]() -> bool { return false; }, [&]() { copyExecuted = true; });

    EXPECT_TRUE(copyExecuted);

    int status;
    waitpid(pid, &status, 0);
}

TEST_F(SubstitutionLockTest, StaleLock_StaleMarkerCausesRetry)
{
    // Test that a lock file with non-zero size (stale marker) is detected
    // and causes a retry
    auto lockPath = getSubstitutionLockPath("stale-marker-test");

    // Create a lock file with a stale marker
    {
        auto fd = openLockFile(lockPath, true);
        writeFull(fd.get(), "d"); // Write stale marker
    }

    // Should be able to acquire lock despite stale marker
    bool copyExecuted = false;
    withSubstitutionLock("stale-marker-test", 1, [&]() -> bool { return false; }, [&]() { copyExecuted = true; });

    EXPECT_TRUE(copyExecuted);
}

TEST_F(SubstitutionLockTest, StaleLock_DetectsInodeMismatch)
{
    // Test that lock acquisition detects when a new file was created
    // at the same path (inode mismatch) while we hold an fd to the old file.
    auto lockPath = getSubstitutionLockPath("stale-inode-test");

    int syncPipe[2];
    ASSERT_EQ(pipe(syncPipe), 0);

    pid_t pid = fork();
    if (pid == 0) {
        close(syncPipe[0]);
        // Child: acquire lock, delete file, create new one, hold briefly
        auto fd = openLockFile(lockPath, true);
        lockFile(fd.get(), ltWrite, false);

        // Delete the file (unlinks while we hold fd)
        unlink(lockPath.c_str());

        // Create a new file at the same path (different inode)
        auto fd2 = openLockFile(lockPath, true);
        // fd still points to old (unlinked) inode
        // fd2 points to new inode at same path

        char ready = '1';
        (void) !write(syncPipe[1], &ready, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        close(syncPipe[1]);
        _exit(0);
    }

    close(syncPipe[1]);
    char ready = 0;
    ASSERT_EQ(read(syncPipe[0], &ready, 1), 1);
    close(syncPipe[0]);

    // Should detect inode mismatch and retry with new file
    bool copyExecuted = false;
    withSubstitutionLock("stale-inode-test", 2, [&]() -> bool { return false; }, [&]() { copyExecuted = true; });

    EXPECT_TRUE(copyExecuted);

    int status;
    waitpid(pid, &status, 0);
}

TEST_F(SubstitutionLockTest, NormalLockRelease_StillWorks)
{
    // Test that normal lock acquisition and release still works
    // after all the stale detection changes
    std::atomic<int> sequenceCounter{0};
    std::vector<int> executionOrder;
    std::mutex mutex;

    auto worker = [&](int id) {
        withSubstitutionLock(
            "normal-release-test",
            10,
            [&]() -> bool { return false; },
            [&]() {
                std::lock_guard<std::mutex> lock(mutex);
                executionOrder.push_back(id);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            });
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 3; i++) {
        threads.emplace_back(worker, i);
    }

    for (auto & t : threads) {
        t.join();
    }

    // All workers completed
    EXPECT_EQ(executionOrder.size(), 3);
}

#endif // !_WIN32

} // namespace nix
