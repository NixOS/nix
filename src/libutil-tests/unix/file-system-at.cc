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

    /* Check that symlinks are not followed and targets are not changed. */

    EXPECT_NO_THROW(
        try { fchmodatTryNoFollow(dirFd.get(), std::filesystem::path("filelink"), 0777); } catch (SysError & e) {
            if (e.errNo != EOPNOTSUPP)
                throw;
        });
    ASSERT_EQ(stat((testDir / "file").c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0644);

    EXPECT_NO_THROW(
        try { fchmodatTryNoFollow(dirFd.get(), std::filesystem::path("dirlink"), 0777); } catch (SysError & e) {
            if (e.errNo != EOPNOTSUPP)
                throw;
        });
    ASSERT_EQ(stat((testDir / "dir").c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0755);

    /* Check fchmodatTryNoFollow works on regular files and directories. */

    EXPECT_NO_THROW(fchmodatTryNoFollow(dirFd.get(), std::filesystem::path("file"), 0600));
    ASSERT_EQ(stat((testDir / "file").c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600);

    EXPECT_NO_THROW((fchmodatTryNoFollow(dirFd.get(), std::filesystem::path("dir"), 0700), 0));
    ASSERT_EQ(stat((testDir / "dir").c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0700);
}

#ifdef __linux__

TEST(fchmodatTryNoFollow, fallbackWithoutProc)
{
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
                _exit(1);

            if (mount(0, "/", 0, MS_PRIVATE | MS_REC, 0) == -1)
                _exit(1);

            if (mount("tmpfs", "/proc", "tmpfs", 0, 0) == -1)
                _exit(1);

            auto dirFd = openDirectory(testDir, FinalSymlink::Follow);
            if (!dirFd)
                exit(1);

            try {
                fchmodatTryNoFollow(dirFd.get(), std::filesystem::path("file"), 0600);
            } catch (SysError & e) {
                _exit(1);
            }

            try {
                fchmodatTryNoFollow(dirFd.get(), std::filesystem::path("link"), 0777);
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
    ASSERT_EQ(stat((testDir / "file").c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600);
}
#endif

} // namespace nix
