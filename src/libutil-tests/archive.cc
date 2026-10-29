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

enum class NarContentMode {
    Skip,
    Drain,
};

struct DrainingFileSystemObjectSink : NullFileSystemObjectSink
{
    void createRegularFile(const CanonPath &, fun<void(CreateRegularFileSink &)> func) override
    {
        struct : CreateRegularFileSink
        {
            void operator()(std::string_view) override {}

            void isExecutable() override {}
        } sink;

        func(sink);
    }
};

size_t parseNar(std::string_view narContents, NarContentMode mode)
{
    StringSource source{narContents};

    if (mode == NarContentMode::Skip) {
        NullFileSystemObjectSink sink;
        parseDump(sink, source);
    } else {
        DrainingFileSystemObjectSink sink;
        parseDump(sink, source);
    }

    return source.pos;
}

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
        std::pair{"invalid-tag-instead-of-contents", "bad archive: expected tag 'contents', got 'AAAAAAAA'"},
        // Unpacking a NAR with a NUL character in a file name should fail.
        std::pair{"nul-character", "bad archive: NAR contains invalid file name 'f"},
        // Likewise for a '.' filename.
        std::pair{"dot", "bad archive: NAR contains invalid file name '.'"},
        // Likewise for a '..' filename.
        std::pair{"dotdot", "bad archive: NAR contains invalid file name '..'"},
        // Likewise for a filename containing a slash.
        std::pair{"slash", "bad archive: NAR contains invalid file name 'x/y'"},
        // Likewise for an empty filename.
        std::pair{"empty", "bad archive: NAR contains invalid file name ''"},
        // Test that the 'executable' field cannot come before the 'contents' field.
        std::pair{"executable-after-contents", "bad archive: expected tag ')', got 'executable'"},
        // Test that the 'name' field cannot come before the 'node' field in a directory entry.
        std::pair{"name-after-node", "bad archive: expected tag 'name'"}));

TEST_F(NarTest, oneByteRegularFileParsesWithBothContentModes)
{
    readTest("regular-one-byte-zero-padding", [&](const std::string & narContents) {
        auto skippingOffset = parseNar(narContents, NarContentMode::Skip);
        auto drainingOffset = parseNar(narContents, NarContentMode::Drain);

        EXPECT_EQ(skippingOffset, narContents.size());
        EXPECT_EQ(drainingOffset, skippingOffset);
    });
}

TEST_F(NarTest, nonZeroContentPaddingFailsWithBothContentModes)
{
    readTest("regular-one-byte-non-zero-padding", [&](const std::string & narContents) {
        for (auto mode : {NarContentMode::Skip, NarContentMode::Drain}) {
            ASSERT_THAT(
                [&]() { parseNar(narContents, mode); },
                ::testing::ThrowsMessage<SerialisationError>(testing::HasSubstrIgnoreANSIMatcher("non-zero padding")));
        }
    });
}

TEST_F(NarTest, truncatedLargeContentsFailsWithBothContentModes)
{
    readTest("regular-truncated-large-contents", [&](const std::string & narContents) {
        for (auto mode : {NarContentMode::Skip, NarContentMode::Drain})
            EXPECT_THROW(parseNar(narContents, mode), EndOfFile);
    });
}

TEST_F(NarTest, truncatedContentPaddingFailsWithBothContentModes)
{
    readTest("regular-truncated-padding", [&](const std::string & narContents) {
        for (auto mode : {NarContentMode::Skip, NarContentMode::Drain})
            EXPECT_THROW(parseNar(narContents, mode), EndOfFile);
    });
}

TEST_F(NarTest, trailingBytesRemainUnreadWithBothContentModes)
{
    readTest("regular-one-byte-zero-padding", [&](const std::string & narContents) {
        auto narWithSentinel = narContents + "sentinel";
        auto skippingOffset = parseNar(narWithSentinel, NarContentMode::Skip);
        auto drainingOffset = parseNar(narWithSentinel, NarContentMode::Drain);

        EXPECT_EQ(skippingOffset, narContents.size());
        EXPECT_EQ(drainingOffset, skippingOffset);
    });
}

} // namespace nix
