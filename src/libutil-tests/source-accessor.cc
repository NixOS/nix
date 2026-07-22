#include "nix/util/fs-sink.hh"
#include "nix/util/file-system.hh"
#include "nix/util/processes.hh"
#include "nix/util/tests/gmock-matchers.hh"

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
        RestoreSink sink(false);
        sink.dstPath = tmpDir;
#ifndef _WIN32
        sink.dirFd = openDirectory(tmpDir, FinalSymlink::Follow);
#endif
        sink.createDirectory(CanonPath("subdir"));
        sink.createRegularFile(CanonPath("file1"), [](CreateRegularFileSink & crf) { crf("content1"); });
        sink.createRegularFile(CanonPath("subdir/file2"), [](CreateRegularFileSink & crf) { crf("content2"); });
        sink.createSymlink(CanonPath("rootlink"), "target");
        sink.createDirectory(CanonPath("a"));
        sink.createSymlink(CanonPath("a/dirlink"), "../subdir");
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

    createDirs(tmpDir / "a" / "b");
    writeFile(tmpDir / "a" / "b" / "f", "old");

    EXPECT_TRUE(accessor->pathExists(CanonPath("a/b/f")));

    deletePath(tmpDir / "a" / "b");
    createDirs(tmpDir / "a" / "b");
    writeFile(tmpDir / "a" / "b" / "g", "new");
    createSymlink("g", tmpDir / "a" / "b" / "l");

    accessor->invalidateCache();

    EXPECT_FALSE(accessor->pathExists(CanonPath("a/b/f")));
    EXPECT_TRUE(accessor->pathExists(CanonPath("a/b/g")));
    EXPECT_THAT(accessor, testing::HasContents(CanonPath("a/b/g"), "new"));
    EXPECT_THAT(accessor, testing::HasDirectory(CanonPath("a/b"), (std::set<std::string>{"g", "l"})));
    EXPECT_THAT(accessor, testing::HasSymlink(CanonPath("a/b/l"), "g"));
}

/* ----------------------------------------------------------------------------
 * RestoreSink non-directory at root (no dirFd)
 * --------------------------------------------------------------------------*/

TEST_F(FSSourceAccessorTest, RestoreSinkRegularFileAtRoot)
{
    auto filePath = tmpDir / "rootfile";
    {
        RestoreSink sink(false);
        sink.dstPath = filePath;
        // No dirFd set - this tests the !dirFd path
        sink.createRegularFile(CanonPath::root, [](CreateRegularFileSink & crf) { crf("root content"); });
    }

    EXPECT_THAT(makeFSSourceAccessor(filePath), testing::HasContents(CanonPath::root, "root content"));
}

TEST_F(FSSourceAccessorTest, RestoreSinkSymlinkAtRoot)
{
#ifdef _WIN32
    GTEST_SKIP() << "symlinks have some problems under Wine";
#endif
    auto linkPath = tmpDir / "rootlink";
    {
        RestoreSink sink(false);
        sink.dstPath = linkPath;
        // No dirFd set - this tests the !dirFd path
        sink.createSymlink(CanonPath::root, "symlink_target");
    }

    EXPECT_THAT(makeFSSourceAccessor(linkPath), testing::HasSymlink(CanonPath::root, "symlink_target"));
}

} // namespace nix
