#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <system_error>

#include "nix/util/file-system-at.hh"
#include "nix/util/file-system.hh"
#include "nix/util/fs-sink.hh"

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

    auto testDir = tmpDir / "root";
    {
        auto parentFd = openDirectory(tmpDir, FinalSymlink::Follow);

        // Create entire test structure through a single RestoreSink
        RestoreSink sink{/*startFsync=*/false};
        sink.parentPath = tmpDir;
        sink.childName = "root";
        sink.dirFd = parentFd.get();
        sink.createDirectory([&](FileSystemObjectSink::OnDirectory & root) {
            root.createChild("link", [](FileSystemObjectSink & s) { s.createSymlink("target"); });
            root.createChild("relative", [](FileSystemObjectSink & s) { s.createSymlink("../relative/path"); });
            root.createChild("absolute", [](FileSystemObjectSink & s) { s.createSymlink("/absolute/path"); });
            root.createChild("medium", [&](FileSystemObjectSink & s) { s.createSymlink(mediumTarget); });
            root.createChild("long", [&](FileSystemObjectSink & s) { s.createSymlink(longTarget); });
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
    }

    auto dirFd = openDirectory(testDir, FinalSymlink::Follow);

    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("link")), OS_STR("target"));
    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("relative")), OS_STR("../relative/path"));
    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("absolute")), OS_STR("/absolute/path"));
    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("medium")), string_to_os_string(mediumTarget));
    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("long")), string_to_os_string(longTarget));
    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("a/b/link")), OS_STR("nested_target"));

    auto subDirFd = openDirectory(testDir / "a", FinalSymlink::Follow);
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

    auto testDir = tmpDir / "root";
    {
        auto parentFd = openDirectory(tmpDir, FinalSymlink::Follow);

        // Create entire test structure through a single RestoreSink
        RestoreSink sink{/*startFsync=*/false};
        sink.parentPath = tmpDir;
        sink.childName = "root";
        sink.dirFd = parentFd.get();
        sink.createDirectory([&](FileSystemObjectSink::OnDirectory & root) {
            root.createChild("a", [&](FileSystemObjectSink & s) {
                s.createDirectory([&](FileSystemObjectSink::OnDirectory & a) {
                    a.createChild(
                        "absolute_symlink", [&](FileSystemObjectSink & s) { s.createSymlink(testDir.string()); });
                    a.createChild("relative_symlink", [](FileSystemObjectSink & s) { s.createSymlink("../."); });
                    a.createChild("broken_symlink", [](FileSystemObjectSink & s) { s.createSymlink("./nonexistent"); });
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

        // Test that creating at an existing path fails
        // a/b/c already exists (as a symlink), so trying to create there should fail
        {
            auto abDir = openDirectory(testDir / "a" / "b", FinalSymlink::Follow);
            bool threw = false;
            try {
                RestoreSink sink{/*startFsync=*/false};
                sink.parentPath = testDir / "a" / "b";
                sink.childName = "c";
                sink.dirFd = abDir.get();
                sink.createDirectory([](FileSystemObjectSink::OnDirectory &) {});
            } catch (const SysError & e) {
                threw = true;
                EXPECT_TRUE(e.is(std::errc::file_exists));
            }
            EXPECT_TRUE(threw) << "Expected SysError to be thrown";
        }

        // a/b/d already exists (as a directory), so trying to create there should fail
        {
            auto abDir = openDirectory(testDir / "a" / "b", FinalSymlink::Follow);
            bool threw = false;
            try {
                RestoreSink sink{/*startFsync=*/false};
                sink.parentPath = testDir / "a" / "b";
                sink.childName = "d";
                sink.dirFd = abDir.get();
                sink.createDirectory([](FileSystemObjectSink::OnDirectory &) {});
            } catch (const SysError & e) {
                threw = true;
                EXPECT_TRUE(e.is(std::errc::file_exists));
            }
            EXPECT_TRUE(threw) << "Expected SysError to be thrown";
        }
    }

    auto dirFd = openDirectory(testDir, FinalSymlink::Follow);

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
