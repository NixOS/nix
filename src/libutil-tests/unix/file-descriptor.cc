#include <gtest/gtest.h>

#include "nix/util/file-descriptor.hh"
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
 * openFileEnsureBeneathNoSymlinks
 * --------------------------------------------------------------------------*/

TEST(openFileEnsureBeneathNoSymlinks, works)
{
    std::filesystem::path tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);

    {
        RestoreSink sink(/*startFsync=*/false);
        sink.dstPath = tmpDir;
        sink.dirFd = openDirectory(tmpDir);
        sink.createDirectory(CanonPath("a"));
        sink.createDirectory(CanonPath("c"));
        sink.createDirectory(CanonPath("c/d"));
        sink.createRegularFile(CanonPath("c/d/regular"), [](CreateRegularFileSink & crf) { crf("some contents"); });
        sink.createSymlink(CanonPath("a/absolute_symlink"), tmpDir.string());
        sink.createSymlink(CanonPath("a/relative_symlink"), "../.");
        sink.createSymlink(CanonPath("a/broken_symlink"), "./nonexistent");
        sink.createDirectory(CanonPath("a/b"), [](FileSystemObjectSink & dirSink, const CanonPath & relPath) {
            dirSink.createDirectory(CanonPath("d"));
            dirSink.createSymlink(CanonPath("c"), "./d");
        });
        sink.createDirectory(CanonPath("a/b/c/e")); // FIXME: This still follows symlinks
        ASSERT_THROW(
            sink.createDirectory(
                CanonPath("a/b/c/f"), [](FileSystemObjectSink & dirSink, const CanonPath & relPath) {}),
            SymlinkNotAllowed);
        ASSERT_THROW(
            sink.createRegularFile(
                CanonPath("a/b/c/regular"), [](CreateRegularFileSink & crf) { crf("some contents"); }),
            SymlinkNotAllowed);
    }

    AutoCloseFD dirFd = openDirectory(tmpDir);

    auto open = [&](std::string_view path, int flags, mode_t mode = 0) {
        return openFileEnsureBeneathNoSymlinks(dirFd.get(), CanonPath(path), flags, mode);
    };

    EXPECT_THROW(open("a/absolute_symlink", O_RDONLY), SymlinkNotAllowed);
    EXPECT_THROW(open("a/relative_symlink", O_RDONLY), SymlinkNotAllowed);
    EXPECT_THROW(open("a/absolute_symlink/a", O_RDONLY), SymlinkNotAllowed);
    EXPECT_THROW(open("a/absolute_symlink/c/d", O_RDONLY), SymlinkNotAllowed);
    EXPECT_THROW(open("a/relative_symlink/c", O_RDONLY), SymlinkNotAllowed);
    EXPECT_THROW(open("a/b/c/d", O_RDONLY), SymlinkNotAllowed);
    EXPECT_EQ(open("a/broken_symlink", O_CREAT | O_WRONLY | O_EXCL, 0666), INVALID_DESCRIPTOR);
    /* Sanity check, no symlink shenanigans and behaves the same as regular openat with O_EXCL | O_CREAT. */
    EXPECT_EQ(errno, EEXIST);
    EXPECT_THROW(open("a/absolute_symlink/broken_symlink", O_CREAT | O_WRONLY | O_EXCL, 0666), SymlinkNotAllowed);
    EXPECT_EQ(open("c/d/regular/a", O_RDONLY), INVALID_DESCRIPTOR);
    EXPECT_EQ(open("c/d/regular", O_RDONLY | O_DIRECTORY), INVALID_DESCRIPTOR);
    EXPECT_TRUE(AutoCloseFD{open("c/d/regular", O_RDONLY)});
    EXPECT_TRUE(AutoCloseFD{open("a/regular", O_CREAT | O_WRONLY | O_EXCL, 0666)});
}

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

    ASSERT_EQ(chmod((tmpDir / "file").c_str(), 0644), 0);
    ASSERT_EQ(chmod((tmpDir / "dir").c_str(), 0755), 0);

    AutoCloseFD dirFd = openDirectory(tmpDir);
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

    ASSERT_EQ(chmod((tmpDir / "file").c_str(), 0644), 0);

    Pid pid = startProcess(
        [&] {
            if (unshare(CLONE_NEWNS) == -1)
                _exit(1);

            if (mount(0, "/", 0, MS_PRIVATE | MS_REC, 0) == -1)
                _exit(1);

            if (mount("tmpfs", "/proc", "tmpfs", 0, 0) == -1)
                _exit(1);

            AutoCloseFD dirFd = openDirectory(tmpDir);
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
