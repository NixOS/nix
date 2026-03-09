#include "nix/util/fs-sink.hh"
#include "nix/util/file-system.hh"
#include "nix/util/file-system-at.hh"
#include "nix/util/processes.hh"

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
    auto parentFd = openDirectory(tmpDir, FinalSymlink::Follow);

    // Create entire test structure through a single RestoreSink
    RestoreSink sink{parentFd.get(), "root", /*startFsync=*/false};
    sink.createDirectory([](FileSystemObjectSink::OnDirectory & root) {
        root.createChild("file1", [](FileSystemObjectSink & s) {
            s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("content1"); });
        });
        root.createChild("subdir", [](FileSystemObjectSink & s) {
            s.createDirectory([](FileSystemObjectSink::OnDirectory & subdir) {
                subdir.createChild("file2", [](FileSystemObjectSink & s) {
                    s.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("content2"); });
                });
            });
        });
        root.createChild("rootlink", [](FileSystemObjectSink & s) { s.createSymlink("target"); });
        root.createChild("a", [](FileSystemObjectSink & s) {
            s.createDirectory([](FileSystemObjectSink::OnDirectory & a) {
                a.createChild("dirlink", [](FileSystemObjectSink & s) { s.createSymlink("../subdir"); });
            });
        });
    });

    auto testDir = tmpDir / "root";

    EXPECT_THAT(makeFSSourceAccessor(testDir / "file1"), HasContents(CanonPath::root, "content1"));
    EXPECT_THAT(makeFSSourceAccessor(testDir / "rootlink"), HasSymlink(CanonPath::root, "target"));
    EXPECT_THAT(
        makeFSSourceAccessor(testDir),
        HasDirectory(CanonPath::root, std::set<std::string>{"file1", "subdir", "rootlink", "a"}));
    EXPECT_THAT(
        makeFSSourceAccessor(testDir / "subdir"), HasDirectory(CanonPath::root, std::set<std::string>{"file2"}));

    {
        auto accessor = makeFSSourceAccessor(testDir);
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
        auto accessor = makeFSSourceAccessor(testDir / "nonexistent");
        EXPECT_FALSE(accessor->maybeLstat(CanonPath::root));
        EXPECT_THROW(accessor->readFile(CanonPath::root), SystemError);
    }

    {
        auto accessor = makeFSSourceAccessor(testDir, true);
        EXPECT_EQ(accessor->getLastModified(), 0);
        accessor->maybeLstat(CanonPath("file1"));
        EXPECT_GT(accessor->getLastModified(), 0);
    }
}

/* ----------------------------------------------------------------------------
 * RestoreSink non-directory at root (no dirFd)
 * --------------------------------------------------------------------------*/

TEST_F(FSSourceAccessorTest, RestoreSinkRegularFileAtRoot)
{
    auto filePath = tmpDir / "rootfile";
    auto parentFd = openDirectory(tmpDir, FinalSymlink::Follow);
    RestoreSink sink{parentFd.get(), "rootfile", /*startFsync=*/false};
    sink.createRegularFile(false, [](FileSystemObjectSink::OnRegularFile & crf) { crf("root content"); });

    EXPECT_THAT(makeFSSourceAccessor(filePath), HasContents(CanonPath::root, "root content"));
}

TEST_F(FSSourceAccessorTest, RestoreSinkSymlinkAtRoot)
{
    auto linkPath = tmpDir / "rootlink2";
    auto parentFd = openDirectory(tmpDir, FinalSymlink::Follow);
    RestoreSink sink{parentFd.get(), "rootlink2", /*startFsync=*/false};
    sink.createSymlink("symlink_target");

    EXPECT_THAT(makeFSSourceAccessor(linkPath), HasSymlink(CanonPath::root, "symlink_target"));
}

} // namespace nix
