#include <gtest/gtest.h>

#include "nix/store/pathlocks.hh"
#include "nix/util/file-system.hh"

namespace nix {

namespace {

/**
 * Lock tests use two separately opened handles to the same file, since
 * locks belong to an open file (description), so those conflict even
 * within one process, on Unix (`flock`) and Windows (`LockFileEx`)
 * alike.
 */
class LockFileTest : public ::testing::Test
{
protected:
    std::filesystem::path tmpDir;
    std::unique_ptr<AutoDelete> delTmpDir;
    std::filesystem::path lockPath;

    void SetUp() override
    {
        tmpDir = createTempDir();
        delTmpDir = std::make_unique<AutoDelete>(tmpDir, true);
        lockPath = tmpDir / "lock";
    }

    AutoCloseFD open()
    {
        auto fd = openLockFile(lockPath, true);
        EXPECT_TRUE(fd);
        return fd;
    }
};

} // namespace

TEST_F(LockFileTest, exclusiveConflictsWithExclusive)
{
    auto a = open();
    auto b = open();
    ASSERT_TRUE(lockFile(a.value().get(), ltWrite, false));
    ASSERT_FALSE(lockFile(b.value().get(), ltWrite, false));
}

TEST_F(LockFileTest, sharedLocksCoexist)
{
    auto a = open();
    auto b = open();
    ASSERT_TRUE(lockFile(a.get(), ltRead, false));
    ASSERT_TRUE(lockFile(b.get(), ltRead, false));
}

TEST_F(LockFileTest, sharedConflictsWithExclusive)
{
    auto a = open();
    auto b = open();
    ASSERT_TRUE(lockFile(a.get(), ltRead, false));
    ASSERT_FALSE(lockFile(b.get(), ltWrite, false));
}

TEST_F(LockFileTest, exclusiveConflictsWithShared)
{
    auto a = open();
    auto b = open();
    ASSERT_TRUE(lockFile(a.get(), ltWrite, false));
    ASSERT_FALSE(lockFile(b.get(), ltRead, false));
}

TEST_F(LockFileTest, unlockReleases)
{
    auto a = open();
    auto b = open();
    ASSERT_TRUE(lockFile(a.get(), ltWrite, false));
    ASSERT_FALSE(lockFile(b.get(), ltWrite, false));
    ASSERT_TRUE(lockFile(a.get(), ltNone, false));
    ASSERT_TRUE(lockFile(b.get(), ltWrite, false));
}

TEST_F(LockFileTest, closingReleases)
{
    auto b = open();
    {
        auto a = open();
        ASSERT_TRUE(lockFile(a.get(), ltWrite, false));
        ASSERT_FALSE(lockFile(b.get(), ltWrite, false));
    }
    ASSERT_TRUE(lockFile(b.get(), ltWrite, false));
}

TEST_F(LockFileTest, downgradeAllowsShared)
{
    auto a = open();
    auto b = open();
    ASSERT_TRUE(lockFile(a.get(), ltWrite, false));
    ASSERT_TRUE(lockFile(a.get(), ltRead, false));
    ASSERT_TRUE(lockFile(b.get(), ltRead, false));
    ASSERT_FALSE(lockFile(b.get(), ltWrite, false));
}

/**
 * The garbage collector locks temporary directories in the store this
 * way (see `LocalStore::createTempDirInStore()`).
 */
TEST_F(LockFileTest, directory)
{
#ifdef _WIN32
    /* TODO: Byte-range locks are for files; Wine rejects them on a
       directory handle (`ERROR_INVALID_HANDLE`), and we believe Windows
       does too. So temporary directories in the store need some other way
       of being locked on Windows. */
    GTEST_SKIP() << "can't lock directories on Windows";
#endif
    auto dir = tmpDir / "dir";
    createDir(dir);
    auto a = openDirectory(dir, FinalSymlink::DontFollow);
    auto b = openDirectory(dir, FinalSymlink::DontFollow);
    ASSERT_TRUE(a && b);
    ASSERT_TRUE(lockFile(a.get(), ltWrite, false));
    ASSERT_FALSE(lockFile(b.get(), ltWrite, false));
}

} // namespace nix
