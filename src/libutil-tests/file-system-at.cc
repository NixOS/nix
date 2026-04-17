#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <system_error>

#include "nix/util/file-system-at.hh"
#include "nix/util/file-system.hh"
#include "nix/util/fs-sink.hh"
#include "nix/util/tests/gmock-matchers.hh"
#ifdef _WIN32
#  include "nix/util/windows-environment.hh"
#endif

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

    bool hasLongSymlinks = true;
    auto testDir = tmpDir / "root";
    {
        auto parentFd = openDirectory(tmpDir, FinalSymlink::Follow);

        // Create entire test structure through a single RestoreSink
        RestoreSink sink{parentFd.get(), "root", /*startFsync=*/false};
        try {
            sink.createDirectory([&](FileSystemObjectSink::OnDirectory & root) {
                root.createChild("link", [](FileSystemObjectSink & s) { s.createSymlink("target"); });
                root.createChild("relative", [](FileSystemObjectSink & s) { s.createSymlink("../relative/path"); });
                root.createChild("absolute", [](FileSystemObjectSink & s) { s.createSymlink("/absolute/path"); });
                try {
                    root.createChild("medium", [&](FileSystemObjectSink & s) { s.createSymlink(mediumTarget); });
                    root.createChild("long", [&](FileSystemObjectSink & s) { s.createSymlink(longTarget); });
                } catch (SystemError & e) {
                    if (e.is(std::errc::filename_too_long)) {
                        hasLongSymlinks = false;
                    } else {
                        throw;
                    }
            }
            root.createChild("a", [](FileSystemObjectSink & s) {
                s.createDirectory([](FileSystemObjectSink::OnDirectory & a) {
                    a.createChild("b", [](FileSystemObjectSink & s) {
                        s.createDirectory([](FileSystemObjectSink::OnDirectory & b) {
                            b.createChild("link", [](FileSystemObjectSink & s) { s.createSymlink("nested_target"); });
                        });
                    });
                });
            });
            root.createChild("regular", [](FileSystemObjectSink & s) {
                s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile &) {});
            });
            root.createChild(
                "dir", [](FileSystemObjectSink & s) { s.createDirectory([](FileSystemObjectSink::OnDirectory &) {}); });
        });
        } catch (SystemError &) {
#ifdef _WIN32
            // It works in @ericson2314's local wine build, but not in the Nix sandbox
            if (windows::isWine())
                GTEST_SKIP() << "symlink creation not supported in Wine only on some file systems";
#endif
            throw;
        }
    }

    auto dirFd = openDirectory(testDir, FinalSymlink::Follow);

    EXPECT_EQ(readLinkAt(dirFd.get(), std::filesystem::path("link")), OS_STR("target"));
    EXPECT_EQ(readLinkAt(dirFd.get(), std::filesystem::path("relative")), OS_STR("../relative/path"));
    EXPECT_EQ(readLinkAt(dirFd.get(), std::filesystem::path("absolute")), OS_STR("/absolute/path"));
    if (hasLongSymlinks) {
        EXPECT_EQ(readLinkAt(dirFd.get(), std::filesystem::path("medium")), string_to_os_string(mediumTarget));
        EXPECT_EQ(readLinkAt(dirFd.get(), std::filesystem::path("long")), string_to_os_string(longTarget));
    }
    EXPECT_EQ(readLinkAt(dirFd.get(), std::filesystem::path("a/b/link")), OS_STR("nested_target"));

    auto subDirFd = openDirectory(testDir / "a", FinalSymlink::Follow);
    EXPECT_EQ(readLinkAt(subDirFd.get(), std::filesystem::path("b/link")), OS_STR("nested_target"));

    // Test error cases - expect SystemError on both platforms
    EXPECT_THROW(readLinkAt(dirFd.get(), std::filesystem::path("regular")), SystemError);
    EXPECT_THROW(readLinkAt(dirFd.get(), std::filesystem::path("dir")), SystemError);
    EXPECT_THROW(readLinkAt(dirFd.get(), std::filesystem::path("nonexistent")), SystemError);
}

/* ----------------------------------------------------------------------------
 * openFileEnsureBeneathNoSymlinks
 * --------------------------------------------------------------------------*/

TEST(openFileEnsureBeneathNoSymlinks, works)
{
    std::filesystem::path tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);

    auto testDir = tmpDir / "root";
    {
        auto parentFd = openDirectory(tmpDir, FinalSymlink::Follow);

        // Create entire test structure through a single RestoreSink
        RestoreSink sink{parentFd.get(), "root", /*startFsync=*/false};
        try {
            sink.createDirectory([&](FileSystemObjectSink::OnDirectory & root) {
                root.createChild("a", [&](FileSystemObjectSink & s) {
                    s.createDirectory([&](FileSystemObjectSink::OnDirectory & a) {
                        a.createChild(
                            "absolute_symlink", [&](FileSystemObjectSink & s) { s.createSymlink(testDir.string()); });
                        a.createChild("relative_symlink", [](FileSystemObjectSink & s) { s.createSymlink("../."); });
                        a.createChild(
                            "broken_symlink", [](FileSystemObjectSink & s) { s.createSymlink("./nonexistent"); });
                        a.createChild("b", [](FileSystemObjectSink & s) {
                            s.createDirectory([](FileSystemObjectSink::OnDirectory & b) {
                                b.createChild("d", [](FileSystemObjectSink & s) {
                                    s.createDirectory([](FileSystemObjectSink::OnDirectory &) {});
                                });
                                b.createChild("c", [](FileSystemObjectSink & s) { s.createSymlink("./d"); });
                            });
                        });
                    });
                });
                root.createChild("c", [](FileSystemObjectSink & s) {
                    s.createDirectory([](FileSystemObjectSink::OnDirectory & c) {
                        c.createChild("d", [](FileSystemObjectSink & s) {
                            s.createDirectory([](FileSystemObjectSink::OnDirectory & d) {
                                d.createChild("regular", [](FileSystemObjectSink & s) {
                                    s.createRegularFile(
                                        false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("some contents"); });
                                });
                            });
                        });
                    });
                });
            });
        } catch (SystemError &) {
#ifdef _WIN32
            // It works in @ericson2314's local wine build, but not in the Nix sandbox
            if (windows::isWine())
                GTEST_SKIP() << "symlink creation not supported in Wine only on some file systems";
#endif
            throw;
        }
        });

        // Test that creating at an existing path fails
        // a/b/c already exists (as a symlink), so trying to create there should fail
        {
            auto abDir = openDirectory(testDir / "a" / "b", FinalSymlink::Follow);
            bool threw = false;
            try {
                RestoreSink sink{abDir.get(), "c", /*startFsync=*/false};
                sink.createDirectory([](FileSystemObjectSink::OnDirectory &) {});
            } catch (const SystemError & e) {
                threw = true;
                EXPECT_TRUE(e.is(std::errc::file_exists));
            }
            EXPECT_TRUE(threw) << "Expected SystemError to be thrown";
        }

        // a/b/d already exists (as a directory), so trying to create there should fail
        {
            auto abDir = openDirectory(testDir / "a" / "b", FinalSymlink::Follow);
            bool threw = false;
            try {
                RestoreSink sink{abDir.get(), "d", /*startFsync=*/false};
                sink.createDirectory([](FileSystemObjectSink::OnDirectory &) {});
            } catch (const SystemError & e) {
                threw = true;
                EXPECT_TRUE(e.is(std::errc::file_exists));
            }
            EXPECT_TRUE(threw) << "Expected SystemError to be thrown";
        }
    }

    auto dirFd = openDirectory(testDir, FinalSymlink::Follow);

    /* Helpers that wrap `openFileEnsureBeneathNoSymlinks` to throw
       `SysError` on null-fd failure. This way every failure is an
       exception and tests can use plain `EXPECT_THROW`/`EXPECT_TRUE`
       without juggling errno-vs-throw state. Callers that want to
       check a specific errno can catch `SysError` and inspect
       `.errNo`. */
    auto check = [](std::string_view what, AutoCloseFD fd) {
        if (!fd)
            throw NativeSysError("openFileEnsureBeneathNoSymlinks: %s", what);
        return fd;
    };

    auto openRead = [&](std::string_view path) -> AutoCloseFD {
        return check(
            path,
            openFileEnsureBeneathNoSymlinks(
                dirFd.get(),
                std::filesystem::path(path),
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
                std::filesystem::path(path),
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
            path,
            openFileEnsureBeneathNoSymlinks(
                dirFd.get(), std::filesystem::path(path), O_PATH | O_DIRECTORY | O_CLOEXEC, 0));
    };
    auto openPath = [&](std::string_view path) -> AutoCloseFD {
        return check(
            path, openFileEnsureBeneathNoSymlinks(dirFd.get(), std::filesystem::path(path), O_PATH | O_CLOEXEC, 0));
    };
#endif

    auto openCreateExclusive = [&](std::string_view path) -> AutoCloseFD {
        return check(
            path,
            openFileEnsureBeneathNoSymlinks(
                dirFd.get(),
                std::filesystem::path(path),
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
#ifdef _WIN32
    using nix::testing::ThrowsWinError;
#endif

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

    // Non-symlink failure modes: error codes must survive.
    EXPECT_THAT(
        [&] { openRead("c/d/regular/a"); },
#ifdef _WIN32
        ThrowsWinError(ERROR_DIRECTORY)
#else
        ThrowsSysError(ENOTDIR)
#endif
    );
    EXPECT_THAT(
        [&] { openReadDir("c/d/regular"); },
#ifdef _WIN32
        ThrowsWinError(ERROR_DIRECTORY)
#else
        ThrowsSysError(ENOTDIR)
#endif
    );
    EXPECT_THAT(
        [&] { openRead("c/d/nonexistent"); },
#ifdef _WIN32
        ThrowsWinError(ERROR_FILE_NOT_FOUND)
#else
        ThrowsSysError(ENOENT)
#endif
    );

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

    auto testDir = tmpDir / "root";
    {
        auto parentFd = openDirectory(tmpDir, FinalSymlink::Follow);
        RestoreSink sink{parentFd.get(), "root", /*startFsync=*/false};
        sink.createDirectory([](FileSystemObjectSink::OnDirectory & root) {
            root.createChild("file", [](FileSystemObjectSink & s) {
                s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile &) {});
            });
        });
    }

    auto dirFd = openDirectory(testDir, FinalSymlink::Follow);

    /* The function asserts that callers don't pass `O_NOFOLLOW` — it
       owns symlink policy. Verify the assert fires for every flag
       combination that includes `O_NOFOLLOW`. */
    EXPECT_DEATH(
        openFileEnsureBeneathNoSymlinks(
            dirFd.get(), std::filesystem::path("file"), O_RDONLY | O_NOFOLLOW, 0),
        "O_NOFOLLOW");
    EXPECT_DEATH(
        openFileEnsureBeneathNoSymlinks(
            dirFd.get(), std::filesystem::path("file"), O_RDONLY | O_DIRECTORY | O_NOFOLLOW, 0),
        "O_NOFOLLOW");
#  if defined(__linux__)
    EXPECT_DEATH(
        openFileEnsureBeneathNoSymlinks(
            dirFd.get(), std::filesystem::path("file"), O_PATH | O_NOFOLLOW, 0),
        "O_NOFOLLOW");
    EXPECT_DEATH(
        openFileEnsureBeneathNoSymlinks(
            dirFd.get(), std::filesystem::path("file"), O_PATH | O_DIRECTORY | O_NOFOLLOW, 0),
        "O_NOFOLLOW");
#  endif
}
#endif

} // namespace nix
