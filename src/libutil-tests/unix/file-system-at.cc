#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/util/file-system-at.hh"
#include "nix/util/file-system.hh"
#include "nix/util/fs-sink.hh"
#include "nix/util/processes.hh"
#include "nix/util/tests/gmock-matchers.hh"

#ifdef __linux__
#  include "nix/util/linux-namespaces.hh"

#  include <sys/mount.h>
#endif

namespace nix {

/* ----------------------------------------------------------------------------
 * fchmodatTryNoFollow
 * --------------------------------------------------------------------------*/

TEST(fchmodatTryNoFollow, works)
{
    using namespace nix::unix;

    std::filesystem::path tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);

    {
        RestoreSink sink(/*startFsync=*/false);
        sink.dstPath = tmpDir;
        sink.dirFd = openDirectory(tmpDir, FinalSymlink::Follow);
        sink.createRegularFile(CanonPath("file"), [](CreateRegularFileSink & crf) {});
        sink.createDirectory(CanonPath("dir"));
        sink.createSymlink(CanonPath("filelink"), "file");
        sink.createSymlink(CanonPath("dirlink"), "dir");
    }

    ASSERT_NO_THROW(chmod(tmpDir / "file", 0644));
    ASSERT_NO_THROW(chmod(tmpDir / "dir", 0755));

    auto dirFd = openDirectory(tmpDir, FinalSymlink::Follow);
    ASSERT_TRUE(dirFd);

    using nix::testing::ThrowsSysError;

    /* Check that symlinks are not followed and targets are not changed.

       Whitelist per OS rather than "not Linux", so that an unrecognised
       platform fails loudly at compile time instead of silently getting the
       wrong expectation:

       - Linux: always throws `SysError(EOPNOTSUPP)` — Linux symlinks have no
         mutable mode bits and `fchmodat` with `AT_SYMLINK_NOFOLLOW` is
         unsupported.

       - BSDs and descendants (Darwin, FreeBSD, NetBSD, OpenBSD, DragonFly BSD):
         succeeds, modifying the symlink's own inode mode without touching the
         target.

       - Anything else: unverified — warn and exercise the call so we at least
         detect crashes, but don't assert an outcome.

       The `ASSERT_EQ`/`EXPECT_EQ` below run in every branch and enforce the
       invariant that the *target* file's mode is unchanged, which is the
       property we actually care about. */
    auto expectSymlinkChmod = [&](std::string_view path, mode_t mode) {
#if defined(__linux__)
        EXPECT_THAT([&] { fchmodatTryNoFollow(dirFd.get(), CanonPath(path), mode); }, ThrowsSysError(EOPNOTSUPP));
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) \
    || defined(__DragonFly__)
        EXPECT_NO_THROW(fchmodatTryNoFollow(dirFd.get(), CanonPath(path), mode));
#else
        GTEST_LOG_(WARNING) << "unknown platform: chmod-on-symlink behaviour is not verified for this OS";
        try {
            fchmodatTryNoFollow(dirFd.get(), CanonPath(path), mode);
        } catch (SysError &) {
        }
#endif
    };

    expectSymlinkChmod("filelink", 0777);
    ASSERT_EQ(stat(tmpDir / "file").st_mode & 0777, 0644);

    expectSymlinkChmod("dirlink", 0777);
    ASSERT_EQ(stat(tmpDir / "dir").st_mode & 0777, 0755);

    /* Check fchmodatTryNoFollow works on regular files and directories. */

    EXPECT_NO_THROW(fchmodatTryNoFollow(dirFd.get(), CanonPath("file"), 0600));
    ASSERT_EQ(stat(tmpDir / "file").st_mode & 0777, 0600);

    EXPECT_NO_THROW(fchmodatTryNoFollow(dirFd.get(), CanonPath("dir"), 0700));
    ASSERT_EQ(stat(tmpDir / "dir").st_mode & 0777, 0700);

    EXPECT_THAT([&] { fchmodatTryNoFollow(dirFd.get(), CanonPath("nonexistent"), 0600); }, ThrowsSysError(ENOENT));
}

#ifdef __linux__

TEST(fchmodatTryNoFollow, fallbackWithoutProc)
{
    using namespace nix::unix;

    if (!userNamespacesSupported())
        GTEST_SKIP() << "User namespaces not supported";

    std::filesystem::path tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);

    {
        RestoreSink sink(/*startFsync=*/false);
        sink.dstPath = tmpDir;
        sink.dirFd = openDirectory(tmpDir, FinalSymlink::Follow);
        sink.createRegularFile(CanonPath("file"), [](CreateRegularFileSink & crf) {});
        sink.createSymlink(CanonPath("link"), "file");
    }

    ASSERT_NO_THROW(chmod(tmpDir / "file", 0644));

    Pid pid = startProcess(
        [&] {
            if (unshare(CLONE_NEWNS) == -1)
                _exit(2);

            if (mount(0, "/", 0, MS_PRIVATE | MS_REC, 0) == -1)
                _exit(2);

            if (mount("tmpfs", "/proc", "tmpfs", 0, 0) == -1)
                _exit(2);

            auto dirFd = openDirectory(tmpDir, FinalSymlink::Follow);
            if (!dirFd)
                _exit(3);

            try {
                fchmodatTryNoFollow(dirFd.get(), CanonPath("file"), 0600);
            } catch (SysError & e) {
                _exit(4);
            }

            try {
                fchmodatTryNoFollow(dirFd.get(), CanonPath("link"), 0777);
            } catch (SysError & e) {
                if (e.errNo == EOPNOTSUPP)
                    _exit(0); /* Success. */
            }

            _exit(5); /* Didn't throw the expected exception. */
        },
        {.cloneFlags = CLONE_NEWUSER});

    int status = pid.wait();
    EXPECT_TRUE(WIFEXITED(status));
    if (WEXITSTATUS(status) == 2)
        GTEST_SKIP() << "Could not mount, system may be misconfigured";
    EXPECT_EQ(WEXITSTATUS(status), 0);

    struct ::stat st;
    ASSERT_EQ(stat((tmpDir / "file").c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600);
}
#endif

} // namespace nix
