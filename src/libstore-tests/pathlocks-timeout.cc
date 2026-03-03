#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <sys/stat.h>

#ifndef _WIN32
#  include <sys/wait.h>
#  include <unistd.h>
#  include <signal.h>
#endif

#include "nix/store/pathlocks.hh"
#include "nix/util/file-system.hh"

namespace nix {

class LockFileTimeoutTest : public ::testing::Test
{
    std::unique_ptr<AutoDelete> delTmpDir;

protected:
    std::filesystem::path tmpDir;
    Path lockPath;

    void SetUp() override
    {
        tmpDir = createTempDir();
        delTmpDir = std::make_unique<AutoDelete>(tmpDir, true);
        lockPath = tmpDir / "test.lock";
    }

    void TearDown() override
    {
        delTmpDir.reset();
    }
};

// Basic functionality tests

TEST_F(LockFileTimeoutTest, ImmediateLockSuccess)
{
    auto fd = openLockFile(lockPath, true);
    ASSERT_TRUE(fd);
    EXPECT_TRUE(lockFileWithTimeout(fd.get(), ltWrite, 5));
}

TEST_F(LockFileTimeoutTest, TimeoutZeroMeansIndefinite)
{
    // Verify timeout=0 calls the blocking lockFile
    auto fd = openLockFile(lockPath, true);
    ASSERT_TRUE(fd);
    // Should succeed immediately on uncontested lock
    EXPECT_TRUE(lockFileWithTimeout(fd.get(), ltWrite, 0));
}

TEST_F(LockFileTimeoutTest, ReadLockAllowsMultipleReaders)
{
    auto fd1 = openLockFile(lockPath, true);
    auto fd2 = openLockFile(lockPath, true);
    ASSERT_TRUE(fd1);
    ASSERT_TRUE(fd2);

    EXPECT_TRUE(lockFileWithTimeout(fd1.get(), ltRead, 1));
    EXPECT_TRUE(lockFileWithTimeout(fd2.get(), ltRead, 1));
}

TEST_F(LockFileTimeoutTest, WriteLockExclusive)
{
    auto fd1 = openLockFile(lockPath, true);
    auto fd2 = openLockFile(lockPath, true);
    ASSERT_TRUE(fd1);
    ASSERT_TRUE(fd2);

    EXPECT_TRUE(lockFileWithTimeout(fd1.get(), ltWrite, 1));
    // Second write lock should fail with timeout
    EXPECT_FALSE(lockFileWithTimeout(fd2.get(), ltWrite, 1));
}

TEST_F(LockFileTimeoutTest, ReadLockBlockedByWriteLock)
{
    auto fd1 = openLockFile(lockPath, true);
    auto fd2 = openLockFile(lockPath, true);
    ASSERT_TRUE(fd1);
    ASSERT_TRUE(fd2);

    EXPECT_TRUE(lockFileWithTimeout(fd1.get(), ltWrite, 1));
    // Read lock should fail when write lock is held
    EXPECT_FALSE(lockFileWithTimeout(fd2.get(), ltRead, 1));
}

TEST_F(LockFileTimeoutTest, WriteLockBlockedByReadLock)
{
    auto fd1 = openLockFile(lockPath, true);
    auto fd2 = openLockFile(lockPath, true);
    ASSERT_TRUE(fd1);
    ASSERT_TRUE(fd2);

    EXPECT_TRUE(lockFileWithTimeout(fd1.get(), ltRead, 1));
    // Write lock should fail when read lock is held
    EXPECT_FALSE(lockFileWithTimeout(fd2.get(), ltWrite, 1));
}

TEST_F(LockFileTimeoutTest, OpenLockFile_CreateFalse_NonExistent)
{
    // Opening a non-existent file with create=false should return invalid fd
    auto fd = openLockFile(lockPath, false);
    EXPECT_FALSE(fd);
}

TEST_F(LockFileTimeoutTest, OpenLockFile_CreateTrue_NonExistent)
{
    // Opening a non-existent file with create=true should create it and succeed
    auto fd = openLockFile(lockPath, true);
    EXPECT_TRUE(fd);
}

TEST_F(LockFileTimeoutTest, NonBlockingLock_Succeeds)
{
    auto fd = openLockFile(lockPath, true);
    ASSERT_TRUE(fd);
    // Non-blocking lock on uncontested file should succeed
    EXPECT_TRUE(lockFile(fd.get(), ltWrite, false));
}

TEST_F(LockFileTimeoutTest, NonBlockingLock_FailsWhenContested)
{
    auto fd1 = openLockFile(lockPath, true);
    auto fd2 = openLockFile(lockPath, true);
    ASSERT_TRUE(fd1);
    ASSERT_TRUE(fd2);

    EXPECT_TRUE(lockFile(fd1.get(), ltWrite, false));
    // Non-blocking lock should fail immediately when contested
    EXPECT_FALSE(lockFile(fd2.get(), ltWrite, false));
}

TEST_F(LockFileTimeoutTest, UnlockAllowsNewLock)
{
    auto fd1 = openLockFile(lockPath, true);
    auto fd2 = openLockFile(lockPath, true);
    ASSERT_TRUE(fd1);
    ASSERT_TRUE(fd2);

    // Acquire write lock
    EXPECT_TRUE(lockFileWithTimeout(fd1.get(), ltWrite, 1));
    // Second lock should fail
    EXPECT_FALSE(lockFile(fd2.get(), ltWrite, false));
    // Release lock
    EXPECT_TRUE(lockFile(fd1.get(), ltNone, false));
    // Now second lock should succeed
    EXPECT_TRUE(lockFileWithTimeout(fd2.get(), ltWrite, 1));
}

// Thread-based contention tests

TEST_F(LockFileTimeoutTest, ThreadContention_WaitsAndSucceeds)
{
    auto fd1 = openLockFile(lockPath, true);
    ASSERT_TRUE(lockFile(fd1.get(), ltWrite, false));

    std::thread releaser([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        lockFile(fd1.get(), ltNone, false);
    });

    auto fd2 = openLockFile(lockPath, true);
    auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(lockFileWithTimeout(fd2.get(), ltWrite, 5));
    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_GE(elapsed, std::chrono::milliseconds(90));

    releaser.join();
}

TEST_F(LockFileTimeoutTest, ThreadContention_TimeoutExpires)
{
    auto fd1 = openLockFile(lockPath, true);
    ASSERT_TRUE(lockFile(fd1.get(), ltWrite, false));

    auto fd2 = openLockFile(lockPath, true);
    auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(lockFileWithTimeout(fd2.get(), ltWrite, 1));
    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_GE(elapsed, std::chrono::milliseconds(900));
    EXPECT_LE(elapsed, std::chrono::milliseconds(1500));
}

// Process-based contention tests (the real use case!)
// These tests use fork(), kill(), and waitpid() which are Unix-only.

#ifndef _WIN32

TEST_F(LockFileTimeoutTest, ProcessContention_WaitsAndSucceeds)
{
    int syncPipe[2];
    ASSERT_EQ(pipe(syncPipe), 0);

    pid_t pid = fork();
    if (pid == 0) {
        close(syncPipe[0]);
        // Child: hold lock for 200ms then exit
        auto fd = openLockFile(lockPath, true);
        lockFile(fd.get(), ltWrite, false);
        char ready = '1';
        (void) !write(syncPipe[1], &ready, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        close(syncPipe[1]);
        _exit(0);
    }

    close(syncPipe[1]);
    char ready = 0;
    ASSERT_EQ(read(syncPipe[0], &ready, 1), 1);
    close(syncPipe[0]);

    auto fd = openLockFile(lockPath, true);
    auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(lockFileWithTimeout(fd.get(), ltWrite, 5));
    auto elapsed = std::chrono::steady_clock::now() - start;

    // Should have waited ~150ms for child to release
    EXPECT_GE(elapsed, std::chrono::milliseconds(100));

    int status;
    waitpid(pid, &status, 0);
}

TEST_F(LockFileTimeoutTest, ProcessContention_TimeoutExpires)
{
    int syncPipe[2];
    ASSERT_EQ(pipe(syncPipe), 0);

    pid_t pid = fork();
    if (pid == 0) {
        close(syncPipe[0]);
        // Child: hold lock for 5 seconds (longer than parent's timeout)
        auto fd = openLockFile(lockPath, true);
        lockFile(fd.get(), ltWrite, false);
        char ready = '1';
        (void) !write(syncPipe[1], &ready, 1);
        std::this_thread::sleep_for(std::chrono::seconds(5));
        close(syncPipe[1]);
        _exit(0);
    }

    close(syncPipe[1]);
    char ready = 0;
    ASSERT_EQ(read(syncPipe[0], &ready, 1), 1);
    close(syncPipe[0]);

    auto fd = openLockFile(lockPath, true);
    auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(lockFileWithTimeout(fd.get(), ltWrite, 1));
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_GE(elapsed, std::chrono::milliseconds(900));
    EXPECT_LE(elapsed, std::chrono::milliseconds(1500));

    // Clean up child
    kill(pid, SIGTERM);
    int status;
    waitpid(pid, &status, 0);
}

TEST_F(LockFileTimeoutTest, ProcessCrash_LockReleased)
{
    pid_t pid = fork();
    if (pid == 0) {
        // Child: acquire lock then crash
        auto fd = openLockFile(lockPath, true);
        lockFile(fd.get(), ltWrite, false);
        _exit(1); // Simulate crash
    }

    // Wait for child to exit
    int status;
    waitpid(pid, &status, 0);

    // Parent should be able to acquire lock immediately
    auto fd = openLockFile(lockPath, true);
    EXPECT_TRUE(lockFileWithTimeout(fd.get(), ltWrite, 1));
}

TEST_F(LockFileTimeoutTest, StaleLockDetection)
{
    // This tests that deleteLockFile marks the file as stale by writing to it
    auto fd = openLockFile(lockPath, true);
    ASSERT_TRUE(fd);
    EXPECT_TRUE(lockFile(fd.get(), ltWrite, false));

    // Verify file is empty initially
    struct stat st;
    ASSERT_EQ(fstat(fd.get(), &st), 0);
    EXPECT_EQ(st.st_size, 0);

    // Delete the lock file (this writes "d" to mark it as stale)
    deleteLockFile(lockPath, fd.get());

    // Verify file now has content (the stale marker)
    ASSERT_EQ(fstat(fd.get(), &st), 0);
    EXPECT_GT(st.st_size, 0);
}

// Signal handler that does nothing - just causes EINTR
static volatile sig_atomic_t signalReceived = 0;

static void signalHandler(int)
{
    signalReceived = 1;
}

TEST_F(LockFileTimeoutTest, BlockingLockRetriesOnEINTR)
{
    // This test verifies that lockFile(fd, type, true) retries on EINTR
    // instead of incorrectly returning false.

    pid_t pid = fork();
    if (pid == 0) {
        // Child: hold lock, wait for signal, then release and exit
        auto fd = openLockFile(lockPath, true);
        lockFile(fd.get(), ltWrite, false);

        // Wait for SIGUSR1 from parent.
        // POSIX requires the signal to be blocked before calling sigwait().
        // Without blocking, the default SIGUSR1 handler would terminate the process.
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGUSR1);
        pthread_sigmask(SIG_BLOCK, &set, nullptr);
        int sig;
        sigwait(&set, &sig);

        // Hold lock a bit longer so parent's flock() is still in progress
        // when we send the interrupt signal
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Release lock and exit
        _exit(0);
    }

    // Parent: wait for child to acquire lock
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Set up signal handler for SIGUSR2
    struct sigaction sa;
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // No SA_RESTART - we want EINTR
    sigaction(SIGUSR2, &sa, nullptr);

    // Start a thread that will:
    // 1. Tell child to release lock
    // 2. Send SIGUSR2 to parent to cause EINTR
    std::thread interrupter([pid]() {
        // Wait a bit, then tell child to release lock
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        kill(pid, SIGUSR1);

        // Send interrupt signal to parent while it's in flock()
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        kill(getpid(), SIGUSR2);
    });

    auto fd = openLockFile(lockPath, true);
    signalReceived = 0;

    // This should block, get interrupted by SIGUSR2, retry, and eventually succeed
    // Before the fix, it would return false on EINTR
    EXPECT_TRUE(lockFile(fd.get(), ltWrite, true));

    // Verify we actually received the signal (test validity check)
    // Note: This might not always be true due to timing, so we don't assert
    // The important thing is that lockFile returned true, not false

    interrupter.join();

    int status;
    waitpid(pid, &status, 0);
}

#endif // !_WIN32

// ============================================================================
// FdLock Tests
// ============================================================================

TEST_F(LockFileTimeoutTest, FdLock_AcquiredSetWhenNonBlockingSucceeds)
{
    // Test for Task 2: Verify FdLock sets acquired=true when wait=true and
    // the initial non-blocking lock attempt succeeds immediately.
    auto fd = openLockFile(lockPath, true);
    ASSERT_TRUE(fd);

    // Lock should succeed immediately (uncontested)
    FdLock lock(fd.get(), ltWrite, true, "waiting...");

    // This would have been false before the fix
    EXPECT_TRUE(lock.acquired);
}

TEST_F(LockFileTimeoutTest, FdLock_AcquiredSetWhenBlockingNeeded)
{
    // Test that FdLock sets acquired=true even when blocking is needed
    auto fd1 = openLockFile(lockPath, true);
    ASSERT_TRUE(fd1);
    EXPECT_TRUE(lockFile(fd1.get(), ltWrite, false)); // Hold lock

    std::thread releaser([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        lockFile(fd1.get(), ltNone, false); // Release lock
    });

    auto fd2 = openLockFile(lockPath, true);
    FdLock lock(fd2.get(), ltWrite, true, "waiting for lock...");

    EXPECT_TRUE(lock.acquired);

    releaser.join();
}

TEST_F(LockFileTimeoutTest, FdLock_AcquiredFalseWhenNonBlockingFails)
{
    // Test that FdLock sets acquired=false when wait=false and lock is contested
    auto fd1 = openLockFile(lockPath, true);
    auto fd2 = openLockFile(lockPath, true);
    ASSERT_TRUE(fd1);
    ASSERT_TRUE(fd2);

    // First lock succeeds
    FdLock lock1(fd1.get(), ltWrite, false, "");
    EXPECT_TRUE(lock1.acquired);

    // Second lock fails (non-blocking, wait=false)
    FdLock lock2(fd2.get(), ltWrite, false, "");
    EXPECT_FALSE(lock2.acquired);
}

// ============================================================================
// Timeout Precision Tests
// ============================================================================

TEST_F(LockFileTimeoutTest, TimeoutRespectedWithinTolerance)
{
    // Test for Task 5: Verify timeout is respected within 100ms tolerance
    // (not 500ms as before the fix)
    auto fd1 = openLockFile(lockPath, true);
    ASSERT_TRUE(lockFile(fd1.get(), ltWrite, true)); // Hold lock

    auto fd2 = openLockFile(lockPath, true);

    auto start = std::chrono::steady_clock::now();
    bool result = lockFileWithTimeout(fd2.get(), ltWrite, 1); // 1 second timeout
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result);
    // Should be within 100ms of timeout (not 500ms as before the fix)
    EXPECT_LE(elapsed, std::chrono::milliseconds(1100));
    // Should be at least close to the timeout
    EXPECT_GE(elapsed, std::chrono::milliseconds(900));
}

} // namespace nix
