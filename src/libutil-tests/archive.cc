#include "nix/util/archive.hh"
#include "nix/util/tests/characterization.hh"
#include "nix/util/tests/gmock-matchers.hh"

#include <gtest/gtest.h>

namespace nix {

namespace {

class NarTest : public CharacterizationTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "nars";

public:
    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / (std::string(testStem) + ".nar");
    }
};

class InvalidNarTest : public NarTest, public ::testing::WithParamInterface<std::tuple<std::string, std::string>>
{};

} // namespace

TEST_P(InvalidNarTest, throwsErrorMessage)
{
    const auto & [name, message] = GetParam();
    readTest(name, [&](const std::string & narContents) {
        ASSERT_THAT(
            [&]() {
                StringSource source{narContents};
                NullFileSystemObjectSink sink;
                parseDump(sink, source);
            },
            ::testing::ThrowsMessage<SerialisationError>(testing::HasSubstrIgnoreANSIMatcher(message)));
    });
}

INSTANTIATE_TEST_SUITE_P(
    NarTest,
    InvalidNarTest,
    ::testing::Values(
        std::pair{"invalid-tag-instead-of-contents", "bad archive: expected tag 'contents', got 'AAAAAAAA'"}));

} // namespace nix
