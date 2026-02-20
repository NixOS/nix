#include <gtest/gtest.h>

#include "nix/util/file-system-at.hh"
#include "nix/util/file-system.hh"
#include "nix/util/fs-sink.hh"
#include "nix/util/processes.hh"

#ifdef __linux__
#  include "nix/util/linux-namespaces.hh"

#  include <sys/mount.h>
#endif

#include <cstring>

namespace nix {

using namespace nix::unix;

/* ----------------------------------------------------------------------------
 * fchmodatTryNoFollow
 * --------------------------------------------------------------------------*/

TEST(fchmodatTryNoFollow, works)
{
    std::filesystem::path tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);

    {
        RestoreSink sink(/*startFsync=*/false);
        sink.dstPath = tmpDir;
        sink.dirFd = openDirectory(tmpDir);
        sink.createRegularFile(CanonPath("file"), [](CreateRegularFileSink & crf) {});
        sink.createDirectory(CanonPath("dir"));
        sink.createSymlink(CanonPath("filelink"), "file");
        sink.createSymlink(CanonPath("dirlink"), "dir");
    }

    ASSERT_NO_THROW(chmod(tmpDir / "file", 0644));
    ASSERT_NO_THROW(chmod(tmpDir / "dir", 0755));

    auto dirFd = openDirectory(tmpDir);
    ASSERT_TRUE(dirFd);

    struct ::stat st;

    /* Check that symlinks are not followed and targets are not changed. */

    EXPECT_NO_THROW(
        try { fchmodatTryNoFollow(dirFd.get(), CanonPath("filelink"), 0777); } catch (SysError & e) {
            if (e.errNo != EOPNOTSUPP)
                throw;
        });
    ASSERT_EQ(stat((tmpDir / "file").c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0644);

    EXPECT_NO_THROW(
        try { fchmodatTryNoFollow(dirFd.get(), CanonPath("dirlink"), 0777); } catch (SysError & e) {
            if (e.errNo != EOPNOTSUPP)
                throw;
        });
    ASSERT_EQ(stat((tmpDir / "dir").c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0755);

    /* Check fchmodatTryNoFollow works on regular files and directories. */

    EXPECT_NO_THROW(fchmodatTryNoFollow(dirFd.get(), CanonPath("file"), 0600));
    ASSERT_EQ(stat((tmpDir / "file").c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600);

    EXPECT_NO_THROW((fchmodatTryNoFollow(dirFd.get(), CanonPath("dir"), 0700), 0));
    ASSERT_EQ(stat((tmpDir / "dir").c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0700);
}

#ifdef __linux__

TEST(fchmodatTryNoFollow, fallbackWithoutProc)
{
    if (!userNamespacesSupported())
        GTEST_SKIP() << "User namespaces not supported";

    std::filesystem::path tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);

    {
        RestoreSink sink(/*startFsync=*/false);
        sink.dstPath = tmpDir;
        sink.dirFd = openDirectory(tmpDir);
        sink.createRegularFile(CanonPath("file"), [](CreateRegularFileSink & crf) {});
        sink.createSymlink(CanonPath("link"), "file");
    }

    ASSERT_NO_THROW(chmod(tmpDir / "file", 0644));

    Pid pid = startProcess(
        [&] {
            if (unshare(CLONE_NEWNS) == -1)
                _exit(1);

            if (mount(0, "/", 0, MS_PRIVATE | MS_REC, 0) == -1)
                _exit(1);

            if (mount("tmpfs", "/proc", "tmpfs", 0, 0) == -1)
                _exit(1);

            auto dirFd = openDirectory(tmpDir);
            if (!dirFd)
                exit(1);

            try {
                fchmodatTryNoFollow(dirFd.get(), CanonPath("file"), 0600);
            } catch (SysError & e) {
                _exit(1);
            }

            try {
                fchmodatTryNoFollow(dirFd.get(), CanonPath("link"), 0777);
            } catch (SysError & e) {
                if (e.errNo == EOPNOTSUPP)
                    _exit(0); /* Success. */
            }

            _exit(1); /* Didn't throw the expected exception. */
        },
        {.cloneFlags = CLONE_NEWUSER});

    int status = pid.wait();
    ASSERT_TRUE(statusOk(status));

    struct ::stat st;
    ASSERT_EQ(stat((tmpDir / "file").c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600);
}
#endif

} // namespace nix
