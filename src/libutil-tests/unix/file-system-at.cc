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

    auto testDir = tmpDir / "root";
    {
        auto parentFd = openDirectory(tmpDir, FinalSymlink::Follow);

        // Create entire test structure through a single RestoreSink
        RestoreSink sink{parentFd.get(), "root", /*startFsync=*/false};
        sink.createDirectory([](FileSystemObjectSink::OnDirectory & root) {
            root.createChild("file", [](FileSystemObjectSink & s) {
                s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile &) {});
            });
            root.createChild(
                "dir", [](FileSystemObjectSink & s) { s.createDirectory([](FileSystemObjectSink::OnDirectory &) {}); });
            root.createChild("filelink", [](FileSystemObjectSink & s) { s.createSymlink("file"); });
            root.createChild("dirlink", [](FileSystemObjectSink & s) { s.createSymlink("dir"); });
        });
    }

    ASSERT_NO_THROW(chmod(testDir / "file", 0644));
    ASSERT_NO_THROW(chmod(testDir / "dir", 0755));

    auto dirFd = openDirectory(testDir, FinalSymlink::Follow);
    ASSERT_TRUE(dirFd);

    struct ::stat st;

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
        EXPECT_THAT(
            [&] { fchmodatTryNoFollow(dirFd.get(), std::filesystem::path(path), mode); }, ThrowsSysError(EOPNOTSUPP));
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) \
    || defined(__DragonFly__)
        EXPECT_NO_THROW(fchmodatTryNoFollow(dirFd.get(), std::filesystem::path(path), mode));
#else
        GTEST_LOG_(WARNING) << "unknown platform: chmod-on-symlink behaviour is not verified for this OS";
        try {
            fchmodatTryNoFollow(dirFd.get(), std::filesystem::path(path), mode);
        } catch (SysError &) {
        }
#endif
    };

    expectSymlinkChmod("filelink", 0777);
    ASSERT_EQ(stat((testDir / "file").c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0644);

    expectSymlinkChmod("dirlink", 0777);
    ASSERT_EQ(stat((testDir / "dir").c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0755);

    /* Check fchmodatTryNoFollow works on regular files and directories. */

    EXPECT_NO_THROW(fchmodatTryNoFollow(dirFd.get(), std::filesystem::path("file"), 0600));
    ASSERT_EQ(stat((testDir / "file").c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600);

    EXPECT_NO_THROW(fchmodatTryNoFollow(dirFd.get(), std::filesystem::path("dir"), 0700));
    ASSERT_EQ(stat((testDir / "dir").c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0700);

    EXPECT_THAT(
        [&] { fchmodatTryNoFollow(dirFd.get(), std::filesystem::path("nonexistent"), 0600); }, ThrowsSysError(ENOENT));
}

#ifdef __linux__

TEST(fchmodatTryNoFollow, fallbackWithoutProc)
{
    using namespace nix::unix;

    if (!userNamespacesSupported())
        GTEST_SKIP() << "User namespaces not supported";

    std::filesystem::path tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);

    auto testDir = tmpDir / "root";
    {
        auto parentFd = openDirectory(tmpDir, FinalSymlink::Follow);

        // Create entire test structure through a single RestoreSink
        RestoreSink sink{parentFd.get(), "root", /*startFsync=*/false};
        sink.createDirectory([](FileSystemObjectSink::OnDirectory & root) {
            root.createChild("file", [](FileSystemObjectSink & s) {
                s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile &) {});
            });
            root.createChild("link", [](FileSystemObjectSink & s) { s.createSymlink("file"); });
        });
    }

    ASSERT_NO_THROW(chmod(testDir / "file", 0644));

    Pid pid = startProcess(
        [&] {
            if (unshare(CLONE_NEWNS) == -1)
                _exit(2);

            if (mount(0, "/", 0, MS_PRIVATE | MS_REC, 0) == -1)
                _exit(2);

            if (mount("tmpfs", "/proc", "tmpfs", 0, 0) == -1)
                _exit(2);

            auto dirFd = openDirectory(testDir, FinalSymlink::Follow);
            if (!dirFd)
                _exit(3);

            try {
                fchmodatTryNoFollow(dirFd.get(), std::filesystem::path("file"), 0600);
            } catch (SysError & e) {
                _exit(4);
            }

            try {
                fchmodatTryNoFollow(dirFd.get(), std::filesystem::path("link"), 0777);
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
    ASSERT_EQ(stat((testDir / "file").c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600);
}
#endif

} // namespace nix
