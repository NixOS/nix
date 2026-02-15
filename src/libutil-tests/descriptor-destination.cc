#include <gtest/gtest.h>

#include "nix/util/descriptor-destination.hh"
#include "nix/util/file-system.hh"

namespace nix {

/**
 * Workaround for libstdc++ not handling extended-length paths (`\\?\...`) on Windows.
 * See
 * https://patchwork.sourceware.org/project/gcc/patch/20250510114705.1508-3-johannes.grunenberg@stud.uni-hannover.de/
 */
static std::filesystem::path canonicalPath(const std::filesystem::path & path)
{
#ifdef _WIN32
    auto str = path.native();
    // Strip \\?\ prefix if present
    if (str.starts_with(L"\\\\?\\"))
        return std::filesystem::canonical(std::filesystem::path(str.substr(4)));
#endif
    return std::filesystem::canonical(path);
}

class DescriptorDestinationTest : public ::testing::Test
{
protected:
    std::filesystem::path tmpDir;
    AutoDelete delTmpDir;

    DescriptorDestinationTest()
        : tmpDir(createTempDir())
        , delTmpDir(tmpDir, true)
    {
    }
};

TEST_F(DescriptorDestinationTest, openDirectory)
{
    auto dest = DescriptorDestination::open(tmpDir, FinalSymlink::Follow);
    EXPECT_TRUE(std::holds_alternative<DescriptorDestination::Parent>(dest.raw));
}

TEST_F(DescriptorDestinationTest, openWithParent)
{
    auto subPath = tmpDir / "subdir";
    auto dest = DescriptorDestination::open(subPath, FinalSymlink::Follow);
    ASSERT_TRUE(std::holds_alternative<DescriptorDestination::Parent>(dest.raw));

    auto & parent = std::get<DescriptorDestination::Parent>(dest.raw);
    EXPECT_TRUE(parent.fd);
    EXPECT_EQ(parent.name, "subdir");
}

TEST_F(DescriptorDestinationTest, toPathDirectory)
{
    auto dest = DescriptorDestination::open(tmpDir, FinalSymlink::Follow);
    auto path = dest.toPath();
    EXPECT_EQ(canonicalPath(path), canonicalPath(tmpDir));
}

TEST_F(DescriptorDestinationTest, toPathWithParent)
{
    auto subPath = tmpDir / "subdir";
    auto dest = DescriptorDestination::open(subPath, FinalSymlink::Follow);
    auto path = dest.toPath();
    EXPECT_EQ(canonicalPath(path.parent_path()), canonicalPath(tmpDir));
    EXPECT_EQ(path.filename(), "subdir");
}

TEST_F(DescriptorDestinationTest, openNonExistentParentThrows)
{
    auto badPath = tmpDir / "nonexistent" / "subdir";
    EXPECT_THROW(DescriptorDestination::open(badPath, FinalSymlink::Follow), SystemError);
}

TEST(DescriptorDestination, openRoot)
{
    std::filesystem::path root =
#ifdef _WIN32
        "C:\\";
#else
        "/";
#endif
    auto dest = DescriptorDestination::open(root, FinalSymlink::Follow);
    ASSERT_TRUE(std::holds_alternative<AutoCloseFD>(dest.raw));
    EXPECT_TRUE(std::get<AutoCloseFD>(dest.raw));
}

TEST_F(DescriptorDestinationTest, openSymlinkFollowAbsoluteTarget)
{
    // Create target directory
    auto targetDir = tmpDir / "target";
    createDir(targetDir);

    // Create symlink with absolute path target
    auto linkPath = tmpDir / "link";
    createSymlink(targetDir, linkPath);

    // Open with Follow - should resolve to target directory
    auto dest = DescriptorDestination::open(linkPath, FinalSymlink::Follow);
    ASSERT_TRUE(std::holds_alternative<DescriptorDestination::Parent>(dest.raw));

    auto & parent = std::get<DescriptorDestination::Parent>(dest.raw);
    EXPECT_EQ(parent.name, "target");
    // The parent fd should point to tmpDir (target's parent)
    EXPECT_EQ(canonicalPath(descriptorToPath(parent.fd.get())), canonicalPath(tmpDir));
}

TEST_F(DescriptorDestinationTest, openSymlinkFollowRelativeTarget)
{
    // Create target directory
    auto targetDir = tmpDir / "target";
    createDir(targetDir);

    // Create symlink with relative path target
    auto linkPath = tmpDir / "link";
    createSymlink("target", linkPath);

    // Open with Follow - should resolve to target directory
    auto dest = DescriptorDestination::open(linkPath, FinalSymlink::Follow);
    ASSERT_TRUE(std::holds_alternative<DescriptorDestination::Parent>(dest.raw));

    auto & parent = std::get<DescriptorDestination::Parent>(dest.raw);
    EXPECT_EQ(parent.name, "target");
    // The parent fd should point to tmpDir (target's parent)
    EXPECT_EQ(canonicalPath(descriptorToPath(parent.fd.get())), canonicalPath(tmpDir));
}

TEST_F(DescriptorDestinationTest, openSymlinkDontFollow)
{
    // Create target directory
    auto targetDir = tmpDir / "target";
    createDir(targetDir);

    // Create symlink
    auto linkPath = tmpDir / "link";
    createSymlink(targetDir, linkPath);

    // Open with DontFollow - should NOT resolve symlink
    auto dest = DescriptorDestination::open(linkPath, FinalSymlink::DontFollow);
    ASSERT_TRUE(std::holds_alternative<DescriptorDestination::Parent>(dest.raw));

    auto & parent = std::get<DescriptorDestination::Parent>(dest.raw);
    EXPECT_EQ(parent.name, "link");
    // The parent fd should point to tmpDir (link's parent)
    EXPECT_EQ(canonicalPath(descriptorToPath(parent.fd.get())), canonicalPath(tmpDir));
}

TEST_F(DescriptorDestinationTest, openSymlinkFollowChain)
{
    // Create target directory
    auto targetDir = tmpDir / "final";
    createDir(targetDir);

    // Create chain of symlinks: link1 -> link2 -> final
    auto link2Path = tmpDir / "link2";
    createSymlink("final", link2Path);

    auto link1Path = tmpDir / "link1";
    createSymlink("link2", link1Path);

    // Open with Follow - should resolve entire chain
    auto dest = DescriptorDestination::open(link1Path, FinalSymlink::Follow);
    ASSERT_TRUE(std::holds_alternative<DescriptorDestination::Parent>(dest.raw));

    auto & parent = std::get<DescriptorDestination::Parent>(dest.raw);
    EXPECT_EQ(parent.name, "final");
}

TEST_F(DescriptorDestinationTest, openNonExistentPathFollow)
{
    // Open a path that doesn't exist - should still work (for creation)
    auto nonExistent = tmpDir / "nonexistent";
    auto dest = DescriptorDestination::open(nonExistent, FinalSymlink::Follow);
    ASSERT_TRUE(std::holds_alternative<DescriptorDestination::Parent>(dest.raw));

    auto & parent = std::get<DescriptorDestination::Parent>(dest.raw);
    EXPECT_EQ(parent.name, "nonexistent");
}

TEST_F(DescriptorDestinationTest, openSymlinkFollowWithDotDot)
{
    // Create directory structure: tmpDir/a/target and tmpDir/b/link -> ../a/target
    auto dirA = tmpDir / "a";
    auto dirB = tmpDir / "b";
    createDir(dirA);
    createDir(dirB);

    auto targetDir = dirA / "target";
    createDir(targetDir);

    // Create symlink with ".." in target
    auto linkPath = dirB / "link";
    createSymlink("../a/target", linkPath);

    // Open with Follow - should resolve through ".."
    auto dest = DescriptorDestination::open(linkPath, FinalSymlink::Follow);
    ASSERT_TRUE(std::holds_alternative<DescriptorDestination::Parent>(dest.raw));

    auto & parent = std::get<DescriptorDestination::Parent>(dest.raw);
    EXPECT_EQ(parent.name, "target");
    // The parent fd should point to dirA (target's parent)
    EXPECT_EQ(canonicalPath(descriptorToPath(parent.fd.get())), canonicalPath(dirA));
}

TEST_F(DescriptorDestinationTest, openSymlinkFollowMultiComponentRelative)
{
    // Create directory structure: tmpDir/subdir/target
    auto subdir = tmpDir / "subdir";
    createDir(subdir);

    auto targetDir = subdir / "target";
    createDir(targetDir);

    // Create symlink with multi-component relative target
    auto linkPath = tmpDir / "link";
    createSymlink("subdir/target", linkPath);

    // Open with Follow - should resolve multi-component path
    auto dest = DescriptorDestination::open(linkPath, FinalSymlink::Follow);
    ASSERT_TRUE(std::holds_alternative<DescriptorDestination::Parent>(dest.raw));

    auto & parent = std::get<DescriptorDestination::Parent>(dest.raw);
    EXPECT_EQ(parent.name, "target");
    // The parent fd should point to subdir (target's parent)
    EXPECT_EQ(canonicalPath(descriptorToPath(parent.fd.get())), canonicalPath(subdir));
}

TEST_F(DescriptorDestinationTest, openAtEmptyPath)
{
    // openAt with empty path should return the directory fd itself
    auto dirFd = openDirectory(tmpDir, FinalSymlink::Follow);
    ASSERT_TRUE(dirFd);

    auto dest = DescriptorDestination::openAt(dirFd.get(), std::filesystem::path{}, FinalSymlink::Follow);
    ASSERT_TRUE(std::holds_alternative<AutoCloseFD>(dest.raw));
    EXPECT_TRUE(std::get<AutoCloseFD>(dest.raw));
}

TEST_F(DescriptorDestinationTest, openAtRelativePath)
{
    // Create subdirectory
    auto subdir = tmpDir / "subdir";
    createDir(subdir);

    auto dirFd = openDirectory(tmpDir, FinalSymlink::Follow);
    ASSERT_TRUE(dirFd);

    auto dest = DescriptorDestination::openAt(dirFd.get(), std::filesystem::path{"subdir"}, FinalSymlink::Follow);
    ASSERT_TRUE(std::holds_alternative<DescriptorDestination::Parent>(dest.raw));

    auto & parent = std::get<DescriptorDestination::Parent>(dest.raw);
    EXPECT_EQ(parent.name, "subdir");
}

TEST_F(DescriptorDestinationTest, openAtMultiComponentPath)
{
    // Create nested directories
    auto dirA = tmpDir / "a";
    auto dirB = dirA / "b";
    createDir(dirA);
    createDir(dirB);

    auto dirFd = openDirectory(tmpDir, FinalSymlink::Follow);
    ASSERT_TRUE(dirFd);

    auto dest = DescriptorDestination::openAt(dirFd.get(), std::filesystem::path{"a/b/c"}, FinalSymlink::Follow);
    ASSERT_TRUE(std::holds_alternative<DescriptorDestination::Parent>(dest.raw));

    auto & parent = std::get<DescriptorDestination::Parent>(dest.raw);
    EXPECT_EQ(parent.name, "c");
    // The parent fd should point to dirB
    EXPECT_EQ(canonicalPath(descriptorToPath(parent.fd.get())), canonicalPath(dirB));
}

} // namespace nix
