#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/util/file-system-at.hh"
#include "nix/util/file-system.hh"
#include "nix/util/fs-sink.hh"

namespace nix {

/* ----------------------------------------------------------------------------
 * readLinkAt
 * --------------------------------------------------------------------------*/

TEST(readLinkAt, works)
{
    std::filesystem::path tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);

    constexpr size_t maxPathLength =
#ifdef _WIN32
        260
#else
        PATH_MAX
#endif
        ;
    std::string mediumTarget(maxPathLength / 2, 'x');
    std::string longTarget(maxPathLength - 1, 'y');

#ifdef _WIN32
    try
#endif
    {
        RestoreSink sink{openDirectory(tmpDir, FinalSymlink::Follow), /*startFsync=*/false};
        sink.createSymlink(CanonPath("link"), "target");
        sink.createSymlink(CanonPath("relative"), "../relative/path");
        sink.createSymlink(CanonPath("absolute"), "/absolute/path");
        sink.createSymlink(CanonPath("medium"), mediumTarget);
        sink.createSymlink(CanonPath("long"), longTarget);
        sink.createDirectory(CanonPath("a"));
        sink.createDirectory(CanonPath("a/b"));
        sink.createSymlink(CanonPath("a/b/link"), "nested_target");
        sink.createRegularFile(CanonPath("regular"), [](CreateRegularFileSink &) {});
        sink.createDirectory(CanonPath("dir"));
    }
#ifdef _WIN32
    catch (SystemError &) {
        GTEST_SKIP() << "This works locally for me with Wine, but fails with Wine inside a sandboxed build. Confusing!";
    }
#endif

    auto dirFd = openDirectory(tmpDir, FinalSymlink::Follow);

    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("link")), OS_STR("target"));
    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("relative")), OS_STR("../relative/path"));
    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("absolute")), OS_STR("/absolute/path"));
    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("medium")), string_to_os_string(mediumTarget));
    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("long")), string_to_os_string(longTarget));
    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("a/b/link")), OS_STR("nested_target"));

    auto subDirFd = openDirectory(tmpDir / "a", FinalSymlink::Follow);
    EXPECT_EQ(readLinkAt(subDirFd.get(), CanonPath("b/link")), OS_STR("nested_target"));

    // Test error cases - expect SystemError on both platforms
    EXPECT_THROW(readLinkAt(dirFd.get(), CanonPath("regular")), SystemError);
    EXPECT_THROW(readLinkAt(dirFd.get(), CanonPath("dir")), SystemError);
    EXPECT_THROW(readLinkAt(dirFd.get(), CanonPath("nonexistent")), SystemError);
}

/* ----------------------------------------------------------------------------
 * openFileEnsureBeneathNoSymlinks
 * --------------------------------------------------------------------------*/

TEST(openFileEnsureBeneathNoSymlinks, works)
{
    std::filesystem::path tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);

    {
        RestoreSink sink{openDirectory(tmpDir, FinalSymlink::Follow), /*startFsync=*/false};
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
#ifdef _WIN32
        EXPECT_THROW(sink.createDirectory(CanonPath("a/b/c/e")), SymlinkNotAllowed);
#else
        // FIXME: This still follows symlinks on Unix (incorrectly succeeds)
        sink.createDirectory(CanonPath("a/b/c/e"));
#endif
        // Test that symlinks in intermediate path are detected during nested operations
        EXPECT_THROW(
            sink.createDirectory(
                CanonPath("a/b/c/f"), [](FileSystemObjectSink & dirSink, const CanonPath & relPath) {}),
            SymlinkNotAllowed);
        EXPECT_THROW(
            sink.createRegularFile(
                CanonPath("a/b/c/regular"), [](CreateRegularFileSink & crf) { crf("some contents"); }),
            SymlinkNotAllowed);
    }

    auto dirFd = openDirectory(tmpDir, FinalSymlink::Follow);

    // Helper to open files with platform-specific arguments
    auto openRead = [&](std::string_view path) -> AutoCloseFD {
        return openFileEnsureBeneathNoSymlinks(
            dirFd.get(),
            CanonPath(path),
#ifdef _WIN32
            FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
            0
#else
            O_RDONLY,
            0
#endif
        );
    };

    auto openReadDir = [&](std::string_view path) -> AutoCloseFD {
        return openFileEnsureBeneathNoSymlinks(
            dirFd.get(),
            CanonPath(path),
#ifdef _WIN32
            FILE_READ_ATTRIBUTES | SYNCHRONIZE,
            FILE_DIRECTORY_FILE
#else
            O_RDONLY | O_DIRECTORY,
            0
#endif
        );
    };

    auto openCreateExclusive = [&](std::string_view path) -> AutoCloseFD {
        return openFileEnsureBeneathNoSymlinks(
            dirFd.get(),
            CanonPath(path),
#ifdef _WIN32
            FILE_WRITE_DATA | SYNCHRONIZE,
            0,
            FILE_CREATE // Create new file, fail if exists (equivalent to O_CREAT | O_EXCL)
#else
            O_CREAT | O_WRONLY | O_EXCL,
            0666
#endif
        );
    };

    // Test that symlinks are detected and rejected
    EXPECT_THROW(openRead("a/absolute_symlink"), SymlinkNotAllowed);
    EXPECT_THROW(openRead("a/relative_symlink"), SymlinkNotAllowed);
    EXPECT_THROW(openRead("a/absolute_symlink/a"), SymlinkNotAllowed);
    EXPECT_THROW(openRead("a/absolute_symlink/c/d"), SymlinkNotAllowed);
    EXPECT_THROW(openRead("a/relative_symlink/c"), SymlinkNotAllowed);
    EXPECT_THROW(openRead("a/b/c/d"), SymlinkNotAllowed);
    EXPECT_THROW(openRead("a/broken_symlink"), SymlinkNotAllowed);

#if !defined(_WIN32) && !defined(__CYGWIN__)
    // This returns ELOOP on cygwin when O_NOFOLLOW is used
    EXPECT_FALSE(openCreateExclusive("a/broken_symlink"));
    /* Sanity check, no symlink shenanigans and behaves the same as regular openat with O_EXCL | O_CREAT. */
    EXPECT_EQ(errno, EEXIST);
#endif
    EXPECT_THROW(openCreateExclusive("a/absolute_symlink/broken_symlink"), SymlinkNotAllowed);

    // Test invalid paths
    EXPECT_FALSE(openRead("c/d/regular/a"));
    EXPECT_FALSE(openReadDir("c/d/regular"));

    // Test valid paths work
    EXPECT_TRUE(openRead("c/d/regular"));
    EXPECT_TRUE(openCreateExclusive("a/regular"));
}

} // namespace nix
