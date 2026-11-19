#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "nix/store/builtins/buildenv.hh"
#include "nix/util/archive.hh"
#include "nix/util/file-system.hh"

namespace nix {

namespace {

class BuildProfileTest : public ::testing::Test
{
protected:
    AutoDelete tempDir{createTempDir()};

    Package makePackage(const std::string & name, size_t depth, bool withFile = true)
    {
        auto path = tempDir.path() / name;
        auto dir = path;
        for (size_t i = 0; i < depth; ++i)
            dir /= "d";
        createDirs(dir);
        if (withFile)
            writeFile(dir / name, "");
        return Package{path, true, 5};
    }
};

} // namespace

TEST_F(BuildProfileTest, acceptsMaximumNarDepth)
{
    auto profile = buildProfile(
        {makePackage("first", narMaxDepth - 2),
         makePackage("second", narMaxDepth - 2),
         makePackage("third", narMaxDepth - 2)});

    auto dir = CanonPath(std::vector<std::string>(narMaxDepth - 2, "d"));
    EXPECT_EQ(profile->readDirectory(dir).size(), 3);
    StringSink sink;
    EXPECT_NO_THROW(profile->dumpPath(CanonPath::root, sink));
}

TEST_F(BuildProfileTest, rejectsExcessiveNarDepthWhenExpandingSymlink)
{
    EXPECT_THAT(
        [&] { buildProfile({makePackage("first", narMaxDepth - 1), makePackage("second", narMaxDepth - 1)}); },
        ::testing::ThrowsMessage<Error>(::testing::HasSubstr("exceeds maximum NAR directory depth")));
}

TEST_F(BuildProfileTest, rejectsExcessiveNarDepthWhenMergingDirectory)
{
    EXPECT_THAT(
        [&] {
            buildProfile(
                {makePackage("first", narMaxDepth - 1, false),
                 makePackage("second", narMaxDepth - 1, false),
                 makePackage("third", narMaxDepth - 1)});
        },
        ::testing::ThrowsMessage<Error>(::testing::HasSubstr("exceeds maximum NAR directory depth")));
}

} // namespace nix
