#include "nix/util/fs-sink.hh"
#include "nix/util/file-system.hh"
#include "nix/util/processes.hh"
#include "nix/util/tests/gmock-matchers.hh"

#ifndef _WIN32
#  include <fcntl.h>
#endif

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <rapidcheck/gtest.h>

namespace nix {

MATCHER_P2(HasContents, path, expected, "")
{
    auto stat = arg->maybeLstat(path);
    if (!stat) {
        *result_listener << arg->showPath(path) << " does not exist";
        return false;
    }
    if (stat->type != SourceAccessor::tRegular) {
        *result_listener << arg->showPath(path) << " is not a regular file";
        return false;
    }
    auto actual = arg->readFile(path);
    if (actual != expected) {
        *result_listener << arg->showPath(path) << " has contents " << ::testing::PrintToString(actual);
        return false;
    }
    return true;
}

MATCHER_P2(HasSymlink, path, target, "")
{
    auto stat = arg->maybeLstat(path);
    if (!stat) {
        *result_listener << arg->showPath(path) << " does not exist";
        return false;
    }
    if (stat->type != SourceAccessor::tSymlink) {
        *result_listener << arg->showPath(path) << " is not a symlink";
        return false;
    }
    auto actual = arg->readLink(path);
    if (actual != target) {
        *result_listener << arg->showPath(path) << " points to " << ::testing::PrintToString(actual);
        return false;
    }
    return true;
}

MATCHER_P2(HasDirectory, path, dirents, "")
{
    auto stat = arg->maybeLstat(path);
    if (!stat) {
        *result_listener << arg->showPath(path) << " does not exist";
        return false;
    }
    if (stat->type != SourceAccessor::tDirectory) {
        *result_listener << arg->showPath(path) << " is not a directory";
        return false;
    }
    auto actual = arg->readDirectory(path);
    std::set<std::string> actualKeys, expectedKeys(dirents.begin(), dirents.end());
    for (auto & [k, _] : actual)
        actualKeys.insert(k);
    if (actualKeys != expectedKeys) {
        *result_listener << arg->showPath(path) << " has entries " << ::testing::PrintToString(actualKeys);
        return false;
    }
    return true;
}

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

    EXPECT_THAT(makeFSSourceAccessor(tmpDir / "file1"), HasContents(CanonPath::root, "content1"));
    EXPECT_THAT(makeFSSourceAccessor(tmpDir / "rootlink"), HasSymlink(CanonPath::root, "target"));
    EXPECT_THAT(
        makeFSSourceAccessor(tmpDir),
        HasDirectory(CanonPath::root, std::set<std::string>{"file1", "subdir", "rootlink", "a"}));
    EXPECT_THAT(makeFSSourceAccessor(tmpDir / "subdir"), HasDirectory(CanonPath::root, std::set<std::string>{"file2"}));

    {
        auto accessor = makeFSSourceAccessor(tmpDir);
        EXPECT_THAT(accessor, HasContents(CanonPath("file1"), "content1"));
        EXPECT_THAT(accessor, HasContents(CanonPath("subdir/file2"), "content2"));

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
    EXPECT_THAT(accessor, HasContents(CanonPath("a/b/g"), "new"));
    EXPECT_THAT(accessor, HasDirectory(CanonPath("a/b"), (std::set<std::string>{"g", "l"})));
    EXPECT_THAT(accessor, HasSymlink(CanonPath("a/b/l"), "g"));
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

    createDirs(tmpDir / "root" / "mid" / "sub");
    writeFile(tmpDir / "root" / "mid" / "sub" / "f", "content");

    chmod(tmpDir / "root", 0100);
    chmod(tmpDir / "root" / "mid", 0100);

    /* Search-only accessor root (#16164). */
    auto accessor = makeFSSourceAccessor(tmpDir / "root");
    /* Path resolution through a search-only intermediate directory (#16165). */
    EXPECT_THAT(accessor, HasContents(CanonPath("mid/sub/f"), "content"));
    /* A readable directory can be listed even if its parent is search-only. */
    EXPECT_THAT(accessor, HasDirectory(CanonPath("mid/sub"), std::set<std::string>{"f"}));
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

    EXPECT_THAT(makeFSSourceAccessor(filePath), HasContents(CanonPath::root, "root content"));
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

    EXPECT_THAT(makeFSSourceAccessor(linkPath), HasSymlink(CanonPath::root, "symlink_target"));
}

} // namespace nix
