#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/util/file-system-at.hh"
#include "nix/util/file-system.hh"
#include "nix/util/fs-sink.hh"
#include "nix/util/tests/gmock-matchers.hh"
#include "nix/util/tests/result.hh"
#include "nix/util/system-error.hh"

#ifdef _WIN32
#  include "nix/util/tests/wine.hh"
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
#ifdef _WIN32
    if (windows::isWine() && !windows::wineSymlinksSupported(tmpDir))
        GTEST_SKIP() << "Wine doesn't seem to be able to create symlinks here";
#endif

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
        RestoreSink::Directory root{testing::valueOrFail(openDirectory(tmpDir, FinalSymlink::Follow))};
        root.createChild("link"_filename, [](FileSystemObjectSink & s) { s.createSymlink("target"); });
        root.createChild("relative"_filename, [](FileSystemObjectSink & s) { s.createSymlink("../relative/path"); });
        root.createChild("absolute"_filename, [](FileSystemObjectSink & s) { s.createSymlink("/absolute/path"); });
        try {
            root.createChild("medium"_filename, [&](FileSystemObjectSink & s) { s.createSymlink(mediumTarget); });
            root.createChild("long"_filename, [&](FileSystemObjectSink & s) { s.createSymlink(longTarget); });
        } catch (SystemError & e) {
            if (e.is(std::errc::filename_too_long)) {
                hasLongSymlinks = false;
            } else {
                throw;
            }
        }
        root.createChild("a"_filename, [](FileSystemObjectSink & s) {
            s.createDirectory([](FileSystemObjectSink::OnDirectory & a) {
                a.createChild("b"_filename, [](FileSystemObjectSink & s) {
                    s.createDirectory([](FileSystemObjectSink::OnDirectory & b) {
                        b.createChild(
                            "link"_filename, [](FileSystemObjectSink & s) { s.createSymlink("nested_target"); });
                    });
                });
            });
        });
        root.createChild("regular"_filename, [](FileSystemObjectSink & s) {
            s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile &) {});
        });
        root.createChild("dir"_filename, [](FileSystemObjectSink & s) {
            s.createDirectory([](FileSystemObjectSink::OnDirectory &) {});
        });
    }

    auto dirFd = testing::valueOrFail(openDirectory(tmpDir, FinalSymlink::Follow));

    EXPECT_EQ(readLinkAt(dirFd.get(), OsFilename::fromPathAsserting("link")), OS_STR("target"));
    EXPECT_EQ(readLinkAt(dirFd.get(), OsFilename::fromPathAsserting("relative")), OS_STR("../relative/path"));
    EXPECT_EQ(readLinkAt(dirFd.get(), OsFilename::fromPathAsserting("absolute")), OS_STR("/absolute/path"));
    if (hasLongSymlinks) {
        EXPECT_EQ(readLinkAt(dirFd.get(), OsFilename::fromPathAsserting("medium")), string_to_os_string(mediumTarget));
        EXPECT_EQ(readLinkAt(dirFd.get(), OsFilename::fromPathAsserting("long")), string_to_os_string(longTarget));
    }
    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("a/b/link")), OS_STR("nested_target"));

    auto subDirFd = testing::valueOrFail(openDirectory(tmpDir / "a", FinalSymlink::Follow));
    EXPECT_EQ(readLinkAt(subDirFd.get(), CanonPath("b/link")), OS_STR("nested_target"));

    // Test error cases - expect SystemError on both platforms
    EXPECT_THROW(readLinkAt(dirFd.get(), OsFilename::fromPathAsserting("regular")), SystemError);
    EXPECT_THROW(readLinkAt(dirFd.get(), OsFilename::fromPathAsserting("dir")), SystemError);
    EXPECT_THROW(readLinkAt(dirFd.get(), OsFilename::fromPathAsserting("nonexistent")), SystemError);
}

/* ----------------------------------------------------------------------------
 * openFileEnsureBeneathNoSymlinks
 * --------------------------------------------------------------------------*/

TEST(openFileEnsureBeneathNoSymlinks, works)
{
    std::filesystem::path tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);
#ifdef _WIN32
    if (windows::isWine() && !windows::wineSymlinksSupported(tmpDir))
        GTEST_SKIP() << "Wine doesn't seem to be able to create symlinks here";
#endif

    {
        RestoreSink::Directory root{testing::valueOrFail(openDirectory(tmpDir, FinalSymlink::Follow))};
        root.createChild("a"_filename, [&](FileSystemObjectSink & s) {
            s.createDirectory([&](FileSystemObjectSink::OnDirectory & a) {
                a.createChild(
                    "absolute_symlink"_filename, [&](FileSystemObjectSink & s) { s.createSymlink(tmpDir.string()); });
                a.createChild("relative_symlink"_filename, [](FileSystemObjectSink & s) { s.createSymlink("../."); });
                a.createChild(
                    "broken_symlink"_filename, [](FileSystemObjectSink & s) { s.createSymlink("./nonexistent"); });
                a.createChild("b"_filename, [](FileSystemObjectSink & s) {
                    s.createDirectory([](FileSystemObjectSink::OnDirectory & b) {
                        b.createChild("d"_filename, [](FileSystemObjectSink & s) {
                            s.createDirectory([](FileSystemObjectSink::OnDirectory &) {});
                        });
                        b.createChild("c"_filename, [](FileSystemObjectSink & s) { s.createSymlink("./d"); });
                    });
                });
            });
        });
        root.createChild("c"_filename, [](FileSystemObjectSink & s) {
            s.createDirectory([](FileSystemObjectSink::OnDirectory & c) {
                c.createChild("d"_filename, [](FileSystemObjectSink & s) {
                    s.createDirectory([](FileSystemObjectSink::OnDirectory & d) {
                        d.createChild("regular"_filename, [](FileSystemObjectSink & s) {
                            s.createRegularFile(
                                false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("some contents"); });
                        });
                    });
                });
            });
        });
    }

    /* A sink only ever creates new entries, so it can't be made to
       traverse a symlink: creating where one (or anything else) already
       exists fails. */
    {
        RestoreSink::Directory ab{testing::valueOrFail(openDirectory(tmpDir / "a" / "b", FinalSymlink::Follow))};
        for (auto & name : {"c"_filename, "d"_filename}) {
            try {
                ab.createChild(name, [](FileSystemObjectSink & s) {
                    s.createDirectory([](FileSystemObjectSink::OnDirectory &) {});
                });
                ADD_FAILURE() << "creating " << name.str() << " should have failed";
            } catch (SystemError & e) {
                EXPECT_TRUE(e.is(std::errc::file_exists)) << name.str();
            }
        }
    }

    auto dirFd = testing::valueOrFail(openDirectory(tmpDir, FinalSymlink::Follow));

    /* Helpers that wrap `openFileEnsureBeneathNoSymlinks` to throw
       `NativeSysError` on failure. This way every failure is an
       exception and tests can use plain `EXPECT_THROW`/`EXPECT_TRUE`
       without juggling results and throws. Callers that want to
       check a specific error can catch `SysError` and inspect
       `.errNo`. */
    auto check = [](std::string_view what, Result<AutoCloseFD, NativeError> fd) {
        if (!fd)
            throw NativeSysError(fd.error(), "openFileEnsureBeneathNoSymlinks: %s", what);
        return std::move(fd).value();
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
 * recursiveSyncAt
 * --------------------------------------------------------------------------*/

/* Not on Windows, where flushing needs write access that
   `recursiveSyncAt` doesn't open with yet. */
#ifndef _WIN32
TEST(recursiveSyncAt, works)
{
    std::filesystem::path tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);

    auto root = tmpDir / "root";
    {
        RestoreSink::Directory top{testing::valueOrFail(openDirectory(tmpDir, FinalSymlink::Follow))};
        top.createChild("root"_filename, [](FileSystemObjectSink & s) {
            s.createDirectory([](FileSystemObjectSink::OnDirectory & rootDir) {
                rootDir.createChild("sub"_filename, [](FileSystemObjectSink & s) {
                    s.createDirectory([](FileSystemObjectSink::OnDirectory & sub) {
                        sub.createChild("file"_filename, [](FileSystemObjectSink & s) {
                            s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("world"); });
                        });
                    });
                });
                rootDir.createChild("file"_filename, [](FileSystemObjectSink & s) {
                    s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("hello"); });
                });
                /* Dangling, so following it would fail. */
                rootDir.createChild(
                    "dangling"_filename, [](FileSystemObjectSink & s) { s.createSymlink("nonexistent"); });
            });
        });
        top.createChild("dangling"_filename, [](FileSystemObjectSink & s) { s.createSymlink("nonexistent"); });
    }

    auto dirFd = testing::valueOrFail(openDirectory(tmpDir, FinalSymlink::Follow));

    /* A whole tree, including a symlink within it. */
    EXPECT_NO_THROW(recursiveSyncAt(dirFd.get(), OsFilename::fromPathAsserting("root")));

    /* A regular file or symlink at the top. */
    auto rootFd = testing::valueOrFail(openDirectory(root, FinalSymlink::Follow));
    EXPECT_NO_THROW(recursiveSyncAt(rootFd.get(), OsFilename::fromPathAsserting("file")));
    EXPECT_NO_THROW(recursiveSyncAt(dirFd.get(), OsFilename::fromPathAsserting("dangling")));

    EXPECT_THROW(recursiveSyncAt(dirFd.get(), OsFilename::fromPathAsserting("nonexistent")), SystemError);
}
#endif

/* ----------------------------------------------------------------------------
 * deletePathAt
 * --------------------------------------------------------------------------*/

TEST(deletePathAt, works)
{
    std::filesystem::path tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);

    bool symlinks = true;
#ifdef _WIN32
    symlinks = !windows::isWine() || windows::wineSymlinksSupported(tmpDir);
#endif

    auto outside = tmpDir / "outside";
    {
        RestoreSink::Directory top{testing::valueOrFail(openDirectory(tmpDir, FinalSymlink::Follow))};
        top.createChild("outside"_filename, [](FileSystemObjectSink & s) {
            s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("keep me"); });
        });
        top.createChild("a"_filename, [&](FileSystemObjectSink & s) {
            s.createDirectory([&](FileSystemObjectSink::OnDirectory & a) {
                a.createChild("tree"_filename, [&](FileSystemObjectSink & s) {
                    s.createDirectory([&](FileSystemObjectSink::OnDirectory & tree) {
                        tree.createChild("sub"_filename, [](FileSystemObjectSink & s) {
                            s.createDirectory([](FileSystemObjectSink::OnDirectory & sub) {
                                sub.createChild("read-only"_filename, [](FileSystemObjectSink & s) {
                                    s.createRegularFile(
                                        false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("world"); });
                                });
                            });
                        });
                        tree.createChild("file"_filename, [](FileSystemObjectSink & s) {
                            s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("hello"); });
                        });
                        if (symlinks)
                            tree.createChild(
                                "link"_filename, [&](FileSystemObjectSink & s) { s.createSymlink(outside.string()); });
                    });
                });
            });
        });
        if (symlinks)
            top.createChild("link-to-a"_filename, [](FileSystemObjectSink & s) { s.createSymlink("a"); });
    }
    std::filesystem::permissions(
        tmpDir / "a" / "tree" / "sub" / "read-only",
        std::filesystem::perms::owner_write | std::filesystem::perms::group_write
            | std::filesystem::perms::others_write,
        std::filesystem::perm_options::remove);

    auto dirFd = testing::valueOrFail(openDirectory(tmpDir, FinalSymlink::Follow));

    /* A whole tree, through a parent directory. The symlink in it is
       deleted, not followed. */
    uint64_t bytesFreed;
    deletePathAt(dirFd.get(), OsCanonPath::fromPathAsserting(std::filesystem::path{"a"} / "tree"), bytesFreed);
    EXPECT_FALSE(pathExists(tmpDir / "a" / "tree"));
    EXPECT_TRUE(pathExists(tmpDir / "a"));
    EXPECT_EQ(readFile(outside), "keep me");
    EXPECT_GE(bytesFreed, 10u);

    /* Missing, and with a missing parent: not an error. */
    EXPECT_NO_THROW(deletePathAt(dirFd.get(), OsFilename::fromPathAsserting("nonexistent")));
    EXPECT_NO_THROW(
        deletePathAt(dirFd.get(), OsCanonPath::fromPathAsserting(std::filesystem::path{"nonexistent"} / "child")));

    if (symlinks) {
        /* Not through a symlink in the parent directories. */
        EXPECT_THROW(
            deletePathAt(dirFd.get(), OsCanonPath::fromPathAsserting(std::filesystem::path{"link-to-a"} / "x")),
            SymlinkNotAllowed);

        /* A symlink itself. */
        deletePathAt(dirFd.get(), OsFilename::fromPathAsserting("link-to-a"));
        EXPECT_FALSE(pathExists(tmpDir / "link-to-a"));
        EXPECT_TRUE(pathExists(tmpDir / "a"));
    }
}

/* ----------------------------------------------------------------------------
 * movePathAt
 * --------------------------------------------------------------------------*/

TEST(movePathAt, works)
{
    std::filesystem::path tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);

    bool symlinks = true;
#ifdef _WIN32
    symlinks = !windows::isWine() || windows::wineSymlinksSupported(tmpDir);
#endif

    {
        RestoreSink::Directory top{testing::valueOrFail(openDirectory(tmpDir, FinalSymlink::Follow))};
        top.createChild("src"_filename, [&](FileSystemObjectSink & s) {
            s.createDirectory([&](FileSystemObjectSink::OnDirectory & src) {
                src.createChild("tree"_filename, [](FileSystemObjectSink & s) {
                    s.createDirectory([](FileSystemObjectSink::OnDirectory & tree) {
                        tree.createChild("file"_filename, [](FileSystemObjectSink & s) {
                            s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("hello"); });
                        });
                    });
                });
                src.createChild("new"_filename, [](FileSystemObjectSink & s) {
                    s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("new"); });
                });
                src.createChild("read-only"_filename, [](FileSystemObjectSink & s) {
                    s.createDirectory([](FileSystemObjectSink::OnDirectory &) {});
                });
                if (symlinks) {
                    src.createChild("link"_filename, [](FileSystemObjectSink & s) { s.createSymlink("target"); });
                    src.createChild("another"_filename, [](FileSystemObjectSink & s) {
                        s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("x"); });
                    });
                }
            });
        });
        top.createChild("dst"_filename, [&](FileSystemObjectSink & s) {
            s.createDirectory([&](FileSystemObjectSink::OnDirectory & dst) {
                dst.createChild("sub"_filename, [](FileSystemObjectSink & s) {
                    s.createDirectory([](FileSystemObjectSink::OnDirectory & sub) {
                        sub.createChild("replaced"_filename, [](FileSystemObjectSink & s) {
                            s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("old"); });
                        });
                    });
                });
                if (symlinks)
                    dst.createChild("sub-link"_filename, [](FileSystemObjectSink & s) { s.createSymlink("sub"); });
            });
        });
    }

    auto srcFd = testing::valueOrFail(openDirectory(tmpDir / "src", FinalSymlink::Follow));
    auto dstFd = testing::valueOrFail(openDirectory(tmpDir / "dst", FinalSymlink::Follow));

    auto sub = [](std::string_view a, std::string_view b) {
        return OsCanonPath::fromPathAsserting(std::filesystem::path{a} / b);
    };

    /* A directory, between two directory handles, into a subdirectory. */
    movePathAt(srcFd.get(), OsFilename::fromPathAsserting("tree"), dstFd.get(), sub("sub", "tree"));
    EXPECT_FALSE(pathExists(tmpDir / "src" / "tree"));
    EXPECT_EQ(readFile(tmpDir / "dst" / "sub" / "tree" / "file"), "hello");

    /* Replacing an existing file. */
    movePathAt(srcFd.get(), OsFilename::fromPathAsserting("new"), dstFd.get(), sub("sub", "replaced"));
    EXPECT_EQ(readFile(tmpDir / "dst" / "sub" / "replaced"), "new");

#ifndef _WIN32
    /* A read-only directory, which has to be made writable temporarily
       to be moved, but ends up read-only again. */
    nix::chmod(tmpDir / "src" / "read-only", 0555);
    movePathAt(srcFd.get(), OsFilename::fromPathAsserting("read-only"), dstFd.get(), sub("sub", "read-only"));
    EXPECT_EQ(nix::lstat(tmpDir / "dst" / "sub" / "read-only").st_mode & 0777, 0555);
    nix::chmod(tmpDir / "dst" / "sub" / "read-only", 0755);
#endif

    if (symlinks) {
        /* A symlink is moved itself. */
        movePathAt(
            srcFd.get(), OsFilename::fromPathAsserting("link"), dstFd.get(), OsFilename::fromPathAsserting("link"));
        EXPECT_EQ(readLink(tmpDir / "dst" / "link"), "target");

        /* Not through a symlink in the parent directories. */
        EXPECT_THROW(
            movePathAt(srcFd.get(), OsFilename::fromPathAsserting("another"), dstFd.get(), sub("sub-link", "another")),
            SymlinkNotAllowed);
    }
}

#ifndef _WIN32

TEST(movePathAt, restoresPermissionsOnError)
{
    std::filesystem::path tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);

    {
        RestoreSink::Directory top{testing::valueOrFail(openDirectory(tmpDir, FinalSymlink::Follow))};
        top.createChild("src"_filename, [](FileSystemObjectSink & s) {
            s.createDirectory([](FileSystemObjectSink::OnDirectory &) {});
        });
        top.createChild("dst"_filename, [](FileSystemObjectSink & s) {
            s.createDirectory([](FileSystemObjectSink::OnDirectory & dst) {
                dst.createChild("file"_filename, [](FileSystemObjectSink & s) {
                    s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile &) {});
                });
            });
        });
    }
    nix::chmod(tmpDir / "src", 0500);

    auto dirFd = testing::valueOrFail(openDirectory(tmpDir, FinalSymlink::Follow));

    /* A non-empty directory cannot be replaced, which is only found out
       after making the source directory writable to move it. */
    EXPECT_THROW(
        movePathAt(
            dirFd.get(), OsFilename::fromPathAsserting("src"), dirFd.get(), OsFilename::fromPathAsserting("dst")),
        SysError);
    EXPECT_EQ(nix::lstat(tmpDir / "src").st_mode & 0777, 0500);
    nix::chmod(tmpDir / "src", 0755);
}

TEST(movePathAt, symlinkReplaces)
{
    std::filesystem::path tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);

    {
        RestoreSink::Directory top{testing::valueOrFail(openDirectory(tmpDir, FinalSymlink::Follow))};
        top.createChild("link"_filename, [](FileSystemObjectSink & s) { s.createSymlink("dangling"); });
        top.createChild("other-link"_filename, [](FileSystemObjectSink & s) { s.createSymlink("other"); });
        top.createChild("regular"_filename, [](FileSystemObjectSink & s) {
            s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("test"); });
        });
        top.createChild("another-link"_filename, [](FileSystemObjectSink & s) { s.createSymlink("hello"); });
    }

    auto dirFd = testing::valueOrFail(openDirectory(tmpDir, FinalSymlink::Follow));

    /* A symlink replaces a symlink. */
    movePathAt(
        dirFd.get(), OsFilename::fromPathAsserting("link"), dirFd.get(), OsFilename::fromPathAsserting("other-link"));
    EXPECT_EQ(readLink(tmpDir / "other-link"), "dangling");

    /* A symlink replaces a regular file. */
    movePathAt(
        dirFd.get(),
        OsFilename::fromPathAsserting("another-link"),
        dirFd.get(),
        OsFilename::fromPathAsserting("regular"));
    EXPECT_EQ(readLink(tmpDir / "regular"), "hello");
}

#endif

/* ----------------------------------------------------------------------------
 * openFileEnsureBeneathNoSymlinks — O_NOFOLLOW is forbidden
 * --------------------------------------------------------------------------*/

#if !defined(_WIN32)
TEST(openFileEnsureBeneathNoSymlinksDeathTest, rejectsONofollow)
{
    std::filesystem::path tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);

    {
        RestoreSink::Directory root{testing::valueOrFail(openDirectory(tmpDir, FinalSymlink::Follow))};
        root.createChild("file"_filename, [](FileSystemObjectSink & s) {
            s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile & crf) {});
        });
    }

    auto dirFd = testing::valueOrFail(openDirectory(tmpDir, FinalSymlink::Follow));

    /* The function asserts that callers don't pass `O_NOFOLLOW` — it
       owns symlink policy. Verify the assert fires for every flag
       combination that includes `O_NOFOLLOW`. */
    EXPECT_DEATH(
        (void) openFileEnsureBeneathNoSymlinks(
            dirFd.get(), OsFilename::fromPathAsserting("file"), O_RDONLY | O_NOFOLLOW, 0),
        "O_NOFOLLOW");
    EXPECT_DEATH(
        (void) openFileEnsureBeneathNoSymlinks(
            dirFd.get(), OsFilename::fromPathAsserting("file"), O_RDONLY | O_DIRECTORY | O_NOFOLLOW, 0),
        "O_NOFOLLOW");
#  if defined(__linux__)
    EXPECT_DEATH(
        (void) openFileEnsureBeneathNoSymlinks(
            dirFd.get(), OsFilename::fromPathAsserting("file"), O_PATH | O_NOFOLLOW, 0),
        "O_NOFOLLOW");
    EXPECT_DEATH(
        (void) openFileEnsureBeneathNoSymlinks(
            dirFd.get(), OsFilename::fromPathAsserting("file"), O_PATH | O_DIRECTORY | O_NOFOLLOW, 0),
        "O_NOFOLLOW");
#  endif
}
#endif

} // namespace nix
