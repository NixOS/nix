#include "nix/util/fs-sink.hh"
#include "nix/util/file-system.hh"
#include "nix/util/processes.hh"
#include "nix/util/tests/gmock-matchers.hh"
#include "nix/util/tests/result.hh"
#include "nix/util/system-error.hh"

#ifdef _WIN32
#  include "nix/util/tests/wine.hh"
#  include "nix/util/windows-environment.hh"
#else
#  include <fcntl.h>
#endif

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <rapidcheck/gtest.h>

namespace nix {

class FSSourceAccessorTest : public ::testing::Test
{
protected:
    std::filesystem::path tmpDir;
    std::unique_ptr<nix::AutoDelete> delTmpDir;

    void SetUp() override
    {
        tmpDir = nix::createTempDir();
        delTmpDir = std::make_unique<nix::AutoDelete>(tmpDir, true);
    }

    void TearDown() override
    {
        delTmpDir.reset();
    }
};

TEST_F(FSSourceAccessorTest, works)
{
#ifdef _WIN32
    GTEST_SKIP() << "Broken on Windows";
#endif
    {
        RestoreSink::Directory root{testing::valueOrFail(openDirectory(tmpDir, FinalSymlink::Follow))};
        root.createChild("subdir", [](FileSystemObjectSink & s) {
            s.createDirectory([](FileSystemObjectSink::OnDirectory & subdir) {
                subdir.createChild("file2", [](FileSystemObjectSink & s) {
                    s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("content2"); });
                });
            });
        });
        root.createChild("file1", [](FileSystemObjectSink & s) {
            s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("content1"); });
        });
        root.createChild("rootlink", [](FileSystemObjectSink & s) { s.createSymlink("target"); });
        root.createChild("a", [](FileSystemObjectSink & s) {
            s.createDirectory([](FileSystemObjectSink::OnDirectory & a) {
                a.createChild("dirlink", [](FileSystemObjectSink & s) { s.createSymlink("../subdir"); });
            });
        });
    }

    EXPECT_THAT(makeFSSourceAccessor(tmpDir / "file1"), testing::HasContents(CanonPath::root, "content1"));
    EXPECT_THAT(makeFSSourceAccessor(tmpDir / "rootlink"), testing::HasSymlink(CanonPath::root, "target"));
    EXPECT_THAT(
        makeFSSourceAccessor(tmpDir),
        testing::HasDirectory(CanonPath::root, std::set<std::string>{"file1", "subdir", "rootlink", "a"}));
    EXPECT_THAT(
        makeFSSourceAccessor(tmpDir / "subdir"),
        testing::HasDirectory(CanonPath::root, std::set<std::string>{"file2"}));

    {
        auto accessor = makeFSSourceAccessor(tmpDir);
        EXPECT_THAT(accessor, testing::HasContents(CanonPath("file1"), "content1"));
        EXPECT_THAT(accessor, testing::HasContents(CanonPath("subdir/file2"), "content2"));

        EXPECT_TRUE(accessor->pathExists(CanonPath("file1")));
        EXPECT_FALSE(accessor->pathExists(CanonPath("nonexistent")));

        EXPECT_THROW(accessor->readFile(CanonPath("a/dirlink/file2")), SymlinkNotAllowed);
        EXPECT_THROW(accessor->maybeLstat(CanonPath("a/dirlink/file2")), SymlinkNotAllowed);
        EXPECT_THROW(accessor->readDirectory(CanonPath("a/dirlink")), SymlinkNotAllowed);
        EXPECT_THROW(accessor->pathExists(CanonPath("a/dirlink/file2")), SymlinkNotAllowed);
    }

    {
        EXPECT_THROW(makeFSSourceAccessor(tmpDir / "nonexistent"), SystemError);
    }

    {
        auto accessor = makeFSSourceAccessor(tmpDir, true);
        EXPECT_EQ(accessor->getLastModified(), 0);
        accessor->maybeLstat(CanonPath("file1"));
        EXPECT_GT(accessor->getLastModified(), 0);
    }
}

TEST_F(FSSourceAccessorTest, invalidateCacheDropsStaleDirFds)
{
#ifdef _WIN32
    GTEST_SKIP() << "fd-based accessor is Unix-only";
#endif
    auto accessor = makeFSSourceAccessor(tmpDir);

    RestoreSink::forPath(tmpDir / "a", /*startFsync=*/false).createDirectory([](FileSystemObjectSink::OnDirectory & a) {
        a.createChild("b", [](FileSystemObjectSink & s) {
            s.createDirectory([](FileSystemObjectSink::OnDirectory & b) {
                b.createChild("f", [](FileSystemObjectSink & s) {
                    s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("old"); });
                });
            });
        });
    });

    EXPECT_TRUE(accessor->pathExists(CanonPath("a/b/f")));

    deletePath(tmpDir / "a" / "b");
    RestoreSink::forPath(tmpDir / "a" / "b", /*startFsync=*/false)
        .createDirectory([](FileSystemObjectSink::OnDirectory & b) {
            b.createChild("g", [](FileSystemObjectSink & s) {
                s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("new"); });
            });
            b.createChild("l", [](FileSystemObjectSink & s) { s.createSymlink("g"); });
        });

    accessor->invalidateCache();

    EXPECT_FALSE(accessor->pathExists(CanonPath("a/b/f")));
    EXPECT_TRUE(accessor->pathExists(CanonPath("a/b/g")));
    EXPECT_THAT(accessor, testing::HasContents(CanonPath("a/b/g"), "new"));
    EXPECT_THAT(accessor, testing::HasDirectory(CanonPath("a/b"), (std::set<std::string>{"g", "l"})));
    EXPECT_THAT(accessor, testing::HasSymlink(CanonPath("a/b/l"), "g"));
}

/* At the moment, the dir-fd cache resolves a miss by walking up to the deepest cached
   ancestor and re-anchoring the rest of the path onto it. Nothing else
   exercises that: the tests above only ever read a single branch, so the
   lookup either hits exactly or starts from the root.

   Only uses the `SourceAccessor` interface, so this runs on Windows too, where
   it degenerates to a plain nested-read check (that accessor has no cache). */
TEST_F(FSSourceAccessorTest, dirFdCacheReanchorsOnIntermediateHit)
{
    /* Each directory holds a single file `f` with the given contents. */
    auto dirWithF = [](std::string_view contents) {
        return [contents](FileSystemObjectSink & s) {
            s.createDirectory([&](FileSystemObjectSink::OnDirectory & dir) {
                dir.createChild("f", [&](FileSystemObjectSink & s) {
                    s.createRegularFile(false, [&](FileSystemObjectSink::OnRegularFile & crf) { crf(contents); });
                });
            });
        };
    };
    /* Siblings deliberately share a length, so that a component comparison
       which only looked at sizes would confuse them. */
    RestoreSink::forPath(tmpDir / "aaa", /*startFsync=*/false)
        .createDirectory([&](FileSystemObjectSink::OnDirectory & aaa) {
            aaa.createChild("bbb", [&](FileSystemObjectSink & s) {
                s.createDirectory([&](FileSystemObjectSink::OnDirectory & bbb) {
                    bbb.createChild("ccc", dirWithF("in-ccc"));
                    bbb.createChild("ddd", dirWithF("in-ddd"));
                });
            });
            aaa.createChild("zzz", dirWithF("in-zzz"));
        });

    auto accessor = makeFSSourceAccessor(tmpDir);

    /* Populate the cache all the way down one branch. */
    EXPECT_THAT(accessor, testing::HasContents(CanonPath("aaa/bbb/ccc/f"), "in-ccc"));

    /* Sibling of the deepest cached directory: misses, re-anchors on `aaa/bbb`. */
    EXPECT_THAT(accessor, testing::HasContents(CanonPath("aaa/bbb/ddd/f"), "in-ddd"));

    /* Shallower sibling: re-anchors on `aaa` instead. */
    EXPECT_THAT(accessor, testing::HasContents(CanonPath("aaa/zzz/f"), "in-zzz"));

    /* Re-read everything, now served from the cache rather than walked. */
    EXPECT_THAT(accessor, testing::HasContents(CanonPath("aaa/bbb/ccc/f"), "in-ccc"));
    EXPECT_THAT(accessor, testing::HasContents(CanonPath("aaa/bbb/ddd/f"), "in-ddd"));
    EXPECT_THAT(accessor, testing::HasContents(CanonPath("aaa/zzz/f"), "in-zzz"));

    /* A genuine miss under a cached ancestor must stay a miss. */
    EXPECT_FALSE(accessor->pathExists(CanonPath("aaa/bbb/eee/f")));
    EXPECT_FALSE(accessor->pathExists(CanonPath("aaa/bbb/ccc/nope")));
}

/* Directories that are traversable but not listable (mode 0111) should
   still be usable, both as the accessor's root and as intermediate
   directories, since resolving paths through them only needs search
   permission. See https://github.com/NixOS/nix/issues/16164. */
TEST_F(FSSourceAccessorTest, searchOnlyDirectories)
{
#if !defined(O_SEARCH) && !defined(O_PATH)
    GTEST_SKIP() << "needs `O_SEARCH` or `O_PATH` to open directories without read permission";
#else
    if (geteuid() == 0)
        GTEST_SKIP() << "root bypasses permission checks";

    RestoreSink::forPath(tmpDir / "root", /*startFsync=*/false)
        .createDirectory([](FileSystemObjectSink::OnDirectory & root) {
            root.createChild("mid", [](FileSystemObjectSink & s) {
                s.createDirectory([](FileSystemObjectSink::OnDirectory & mid) {
                    mid.createChild("sub", [](FileSystemObjectSink & s) {
                        s.createDirectory([](FileSystemObjectSink::OnDirectory & sub) {
                            sub.createChild("f", [](FileSystemObjectSink & s) {
                                s.createRegularFile(
                                    false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("content"); });
                            });
                        });
                    });
                });
            });
        });

    chmod(tmpDir / "root", 0100);
    chmod(tmpDir / "root" / "mid", 0100);

    /* Search-only accessor root (#16164). */
    auto accessor = makeFSSourceAccessor(tmpDir / "root");
    /* Path resolution through a search-only intermediate directory (#16165). */
    EXPECT_THAT(accessor, testing::HasContents(CanonPath("mid/sub/f"), "content"));
    /* A readable directory can be listed even if its parent is search-only. */
    EXPECT_THAT(accessor, testing::HasDirectory(CanonPath("mid/sub"), std::set<std::string>{"f"}));
    /* `fstat` on the search-only root descriptor itself. */
    EXPECT_EQ(accessor->lstat(CanonPath::root).type, SourceAccessor::tDirectory);
    /* `fstatat` relative to the search-only root descriptor. */
    EXPECT_EQ(accessor->lstat(CanonPath("mid")).type, SourceAccessor::tDirectory);

    /* Listing them still requires read permission. */
    EXPECT_THAT(
        [&] { accessor->readDirectory(CanonPath::root); }, testing::ThrowsSystemError(std::errc::permission_denied));
    EXPECT_THAT(
        [&] { accessor->readDirectory(CanonPath("mid")); }, testing::ThrowsSystemError(std::errc::permission_denied));
#endif
}

/* ----------------------------------------------------------------------------
 * RestoreSink non-directory at root (`RestoreSink::forPath`)
 * --------------------------------------------------------------------------*/

TEST_F(FSSourceAccessorTest, RestoreSinkRegularFileAtRoot)
{
    auto filePath = tmpDir / "rootfile";
    {
        auto sink = RestoreSink::forPath(filePath, /*startFsync=*/false);
        sink.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("root content"); });
    }

    EXPECT_THAT(makeFSSourceAccessor(filePath), testing::HasContents(CanonPath::root, "root content"));
}

TEST_F(FSSourceAccessorTest, RestoreSinkSymlinkAtRoot)
{
#ifdef _WIN32
    if (windows::isWine() && !windows::wineSymlinksSupported(tmpDir))
        GTEST_SKIP() << "Wine doesn't seem to be able to create symlinks here";
#endif
    auto linkPath = tmpDir / "rootlink";
    {
        auto sink = RestoreSink::forPath(linkPath, /*startFsync=*/false);
        sink.createSymlink("symlink_target");
    }

    EXPECT_THAT(makeFSSourceAccessor(linkPath), testing::HasSymlink(CanonPath::root, "symlink_target"));
}

/* As with POSIX, a trailing separator means the destination must be a
   directory. */
TEST_F(FSSourceAccessorTest, RestoreSinkTrailingSeparator)
{
    {
        auto sink = RestoreSink::forPath(tmpDir / "dir" / "", /*startFsync=*/false);
        sink.createDirectory([](FileSystemObjectSink::OnDirectory & dir) {
            dir.createChild("file", [](FileSystemObjectSink & s) {
                s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("content"); });
            });
        });
    }
    EXPECT_THAT(makeFSSourceAccessor(tmpDir / "dir"), testing::HasContents(CanonPath("file"), "content"));

    EXPECT_THAT(
        [&] {
            auto sink = RestoreSink::forPath(tmpDir / "file" / "", /*startFsync=*/false);
            sink.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("content"); });
        },
        testing::ThrowsSystemError(std::errc::is_a_directory));
    EXPECT_THAT(
        [&] {
            auto sink = RestoreSink::forPath(tmpDir / "link" / "", /*startFsync=*/false);
            sink.createSymlink("target");
        },
        testing::ThrowsSystemError(std::errc::is_a_directory));
    EXPECT_FALSE(pathExists(tmpDir / "file"));
    EXPECT_FALSE(pathExists(tmpDir / "link"));
}

} // namespace nix
