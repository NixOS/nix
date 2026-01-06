#include <gtest/gtest.h>

#include "nix/util/file-descriptor.hh"
#include "nix/util/file-system.hh"
#include "nix/util/fs-sink.hh"

#include <cstring>

namespace nix {

/* ----------------------------------------------------------------------------
 * openFileEnsureBeneathNoSymlinks
 * --------------------------------------------------------------------------*/

TEST(openFileEnsureBeneathNoSymlinks, works)
{
    std::filesystem::path tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);
    using namespace nix::unix;

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

} // namespace nix
