#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/util/file-system-at.hh"
#include "nix/util/file-system.hh"
#include "nix/util/fs-sink.hh"
#include "nix/util/tests/gmock-matchers.hh"

namespace nix {

/* ----------------------------------------------------------------------------
 * readLinkAt
 * --------------------------------------------------------------------------*/

TEST(readLinkAt, works)
{
#ifdef _WIN32
    GTEST_SKIP() << "Broken on Windows";
#endif
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

    bool hasLongSymlinks = true;
    {
        RestoreSink sink(/*startFsync=*/false);
        sink.dstPath = tmpDir;
        sink.dirFd = openDirectory(tmpDir, FinalSymlink::Follow);
        sink.createSymlink(CanonPath("link"), "target");
        sink.createSymlink(CanonPath("relative"), "../relative/path");
        sink.createSymlink(CanonPath("absolute"), "/absolute/path");
        try {
            sink.createSymlink(CanonPath("medium"), mediumTarget);
            sink.createSymlink(CanonPath("long"), longTarget);
        } catch (SystemError & e) {
            if (e.is(std::errc::filename_too_long)) {
                hasLongSymlinks = false;
            } else {
                throw;
            }
        }
        sink.createDirectory(CanonPath("a"));
        sink.createDirectory(CanonPath("a/b"));
        sink.createSymlink(CanonPath("a/b/link"), "nested_target");
        sink.createRegularFile(CanonPath("regular"), [](CreateRegularFileSink &) {});
        sink.createDirectory(CanonPath("dir"));
    }

    auto dirFd = openDirectory(tmpDir, FinalSymlink::Follow);

    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("link")), OS_STR("target"));
    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("relative")), OS_STR("../relative/path"));
    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("absolute")), OS_STR("/absolute/path"));
    if (hasLongSymlinks) {
        EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("medium")), string_to_os_string(mediumTarget));
        EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("long")), string_to_os_string(longTarget));
    }
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
#ifdef _WIN32
    GTEST_SKIP() << "Broken on Windows";
#endif
    std::filesystem::path tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);

    {
        RestoreSink sink(/*startFsync=*/false);
        sink.dstPath = tmpDir;
        sink.dirFd = openDirectory(tmpDir, FinalSymlink::Follow);
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

    /* Helpers that wrap `openFileEnsureBeneathNoSymlinks` to throw
       `SysError` on null-fd failure. This way every failure is an
       exception and tests can use plain `EXPECT_THROW`/`EXPECT_TRUE`
       without juggling errno-vs-throw state. Callers that want to
       check a specific errno can catch `SysError` and inspect
       `.errNo`. */
    auto check = [](std::string_view what, AutoCloseFD fd) {
        if (!fd)
            throw SysError("openFileEnsureBeneathNoSymlinks: %s", what);
        return fd;
    };

    auto openRead = [&](std::string_view path) -> AutoCloseFD {
        return check(
            path,
            openFileEnsureBeneathNoSymlinks(
                dirFd.get(),
                CanonPath(path),
#ifdef _WIN32
                FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                0
#else
                O_RDONLY,
                0
#endif
                ));
    };

    auto openReadDir = [&](std::string_view path) -> AutoCloseFD {
        return check(
            path,
            openFileEnsureBeneathNoSymlinks(
                dirFd.get(),
                CanonPath(path),
#ifdef _WIN32
                FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                FILE_DIRECTORY_FILE
#else
                O_RDONLY | O_DIRECTORY,
                0
#endif
                ));
    };

#if defined(__linux__)
    auto openReadDirPath = [&](std::string_view path) -> AutoCloseFD {
        return check(
            path, openFileEnsureBeneathNoSymlinks(dirFd.get(), CanonPath(path), O_PATH | O_DIRECTORY | O_CLOEXEC, 0));
    };
    auto openPath = [&](std::string_view path) -> AutoCloseFD {
        return check(path, openFileEnsureBeneathNoSymlinks(dirFd.get(), CanonPath(path), O_PATH | O_CLOEXEC, 0));
    };
#endif

    auto openCreateExclusive = [&](std::string_view path) -> AutoCloseFD {
        return check(
            path,
            openFileEnsureBeneathNoSymlinks(
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
                ));
    };

    using nix::testing::ThrowsSysError;

    // Symlinks in the path (any position) are rejected.
    EXPECT_THROW(openRead("a/absolute_symlink"), SymlinkNotAllowed);
    EXPECT_THROW(openRead("a/relative_symlink"), SymlinkNotAllowed);
    EXPECT_THROW(openRead("a/absolute_symlink/a"), SymlinkNotAllowed);
    EXPECT_THROW(openRead("a/absolute_symlink/c/d"), SymlinkNotAllowed);
    EXPECT_THROW(openRead("a/relative_symlink/c"), SymlinkNotAllowed);
    EXPECT_THROW(openRead("a/b/c/d"), SymlinkNotAllowed);
    EXPECT_THROW(openRead("a/broken_symlink"), SymlinkNotAllowed);

    /* Trailing symlink with `O_DIRECTORY` (no `O_NOFOLLOW`): the
       syscall reports `ELOOP`, handled by the direct
       `SymlinkNotAllowed` throw in `openFileEnsureBeneathNoSymlinks`. */
    EXPECT_THROW(openReadDir("a/absolute_symlink"), SymlinkNotAllowed);
    EXPECT_THROW(openReadDir("a/relative_symlink"), SymlinkNotAllowed);
    EXPECT_THROW(openReadDir("a/b/c"), SymlinkNotAllowed);
    EXPECT_THROW(openReadDir("a/broken_symlink"), SymlinkNotAllowed);

#if defined(__linux__)
    // Same thing for `O_PATH | O_DIRECTORY` (Linux-only flag).
    EXPECT_THROW(openReadDirPath("a/absolute_symlink"), SymlinkNotAllowed);
    EXPECT_THROW(openReadDirPath("a/relative_symlink"), SymlinkNotAllowed);
    EXPECT_THROW(openReadDirPath("a/b/c"), SymlinkNotAllowed);
    EXPECT_THROW(openReadDirPath("a/broken_symlink"), SymlinkNotAllowed);

    /* `O_PATH` on a trailing symlink is allowed: the caller gets a
       path fd referring to the symlink itself. Interior symlinks are
       still rejected. */
    EXPECT_TRUE(openPath("a/absolute_symlink"));
    EXPECT_TRUE(openPath("a/relative_symlink"));
    EXPECT_TRUE(openPath("a/b/c"));
    EXPECT_TRUE(openPath("a/broken_symlink"));
    EXPECT_THROW(openPath("a/absolute_symlink/a"), SymlinkNotAllowed);
    EXPECT_THROW(openPath("a/absolute_symlink/c/d"), SymlinkNotAllowed);
    EXPECT_THROW(openPath("a/relative_symlink/c"), SymlinkNotAllowed);
    EXPECT_THROW(openPath("a/b/c/d"), SymlinkNotAllowed);
#endif

#if !defined(_WIN32) && !defined(__CYGWIN__)
    /* Sanity check: behaves the same as plain openat with
       `O_EXCL | O_CREAT` on an existing broken-symlink path —
       the symlink counts as "already there". */
    EXPECT_THAT([&] { openCreateExclusive("a/broken_symlink"); }, ThrowsSysError(EEXIST));
#endif
    EXPECT_THROW(openCreateExclusive("a/absolute_symlink/broken_symlink"), SymlinkNotAllowed);

    // Non-symlink failure modes: errno must survive.
    EXPECT_THAT([&] { openRead("c/d/regular/a"); }, ThrowsSysError(ENOTDIR));
    EXPECT_THAT([&] { openReadDir("c/d/regular"); }, ThrowsSysError(ENOTDIR));
    EXPECT_THAT([&] { openRead("c/d/nonexistent"); }, ThrowsSysError(ENOENT));

    // Happy paths.
    EXPECT_TRUE(openRead("c/d/regular"));
    EXPECT_TRUE(openReadDir("c"));
    EXPECT_TRUE(openReadDir("c/d"));
    EXPECT_TRUE(openCreateExclusive("a/regular"));
#if defined(__linux__)
    EXPECT_TRUE(openReadDirPath("c"));
    EXPECT_TRUE(openReadDirPath("c/d"));
    EXPECT_TRUE(openPath("c/d/regular"));
    EXPECT_TRUE(openPath("c"));
#endif
}

/* ----------------------------------------------------------------------------
 * openFileEnsureBeneathNoSymlinks — O_NOFOLLOW is forbidden
 * --------------------------------------------------------------------------*/

#if !defined(_WIN32)
TEST(openFileEnsureBeneathNoSymlinksDeathTest, rejectsONofollow)
{
    std::filesystem::path tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);

    {
        RestoreSink sink(/*startFsync=*/false);
        sink.dstPath = tmpDir;
        sink.dirFd = openDirectory(tmpDir, FinalSymlink::Follow);
        sink.createRegularFile(CanonPath("file"), [](CreateRegularFileSink & crf) {});
    }

    auto dirFd = openDirectory(tmpDir, FinalSymlink::Follow);

    /* The function asserts that callers don't pass `O_NOFOLLOW` — it
       owns symlink policy. Verify the assert fires for every flag
       combination that includes `O_NOFOLLOW`. */
    EXPECT_DEATH(
        openFileEnsureBeneathNoSymlinks(dirFd.get(), CanonPath("file"), O_RDONLY | O_NOFOLLOW, 0), "O_NOFOLLOW");
    EXPECT_DEATH(
        openFileEnsureBeneathNoSymlinks(dirFd.get(), CanonPath("file"), O_RDONLY | O_DIRECTORY | O_NOFOLLOW, 0),
        "O_NOFOLLOW");
#  if defined(__linux__)
    EXPECT_DEATH(openFileEnsureBeneathNoSymlinks(dirFd.get(), CanonPath("file"), O_PATH | O_NOFOLLOW, 0), "O_NOFOLLOW");
    EXPECT_DEATH(
        openFileEnsureBeneathNoSymlinks(dirFd.get(), CanonPath("file"), O_PATH | O_DIRECTORY | O_NOFOLLOW, 0),
        "O_NOFOLLOW");
#  endif
}
#endif

} // namespace nix
