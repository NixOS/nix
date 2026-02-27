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

} // namespace nix
