#include "nix/util/compression.hh"
#include <gtest/gtest.h>
#include <zstd.h>

namespace nix {

/* ----------------------------------------------------------------------------
 * compress / decompress
 * --------------------------------------------------------------------------*/

TEST(compress, noneMethodDoesNothingToTheInput)
{
    auto o = compress(CompressionAlgo::none, "this-is-a-test");

    ASSERT_EQ(o, "this-is-a-test");
}

struct CompressionDecompressionTest : ::testing::WithParamInterface<CompressionAlgo>, public ::testing::Test
{
    static constexpr std::string_view dummyInput = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
};

TEST_P(CompressionDecompressionTest, roundtrip)
{
    auto o = decompress(GetParam(), compress(GetParam(), dummyInput));
    ASSERT_EQ(o, dummyInput);
}

TEST_P(CompressionDecompressionTest, empty)
{
    auto compressed = compress(GetParam(), "");
    if (GetParam() != CompressionAlgo::none)
        ASSERT_FALSE(compressed.empty());
    auto o = decompress(GetParam(), compressed);
    ASSERT_EQ(o, "");
}

TEST_P(CompressionDecompressionTest, roundtripsWithSourceAndSink)
{
    StringSink strSink;
    auto decompressionSink = makeDecompressionSink(CompressionAlgo::bzip2, strSink);
    auto sink = makeCompressionSink(CompressionAlgo::bzip2, *decompressionSink);

    (*sink)(dummyInput);
    sink->finish();
    decompressionSink->finish();

    ASSERT_EQ(strSink.s.c_str(), dummyInput);
}

INSTANTIATE_TEST_SUITE_P(
    CompressionDecompression,
    CompressionDecompressionTest,
    ::testing::Values(
        CompressionAlgo::none,
        CompressionAlgo::xz,
        CompressionAlgo::bzip2,
        CompressionAlgo::brotli,
        CompressionAlgo::zstd),
    [](const ::testing::TestParamInfo<CompressionAlgo> & info) { return showCompressionAlgo(info.param); });

TEST(decompress, decompressNoneCompressed)
{
    auto str = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
    auto o = decompress(CompressionAlgo::none, str);

    ASSERT_EQ(o, str);
}

TEST(decompress, decompressZstdCompressedParallel)
{
    auto str = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
    auto o = decompress(CompressionAlgo::zstd, compress(CompressionAlgo::zstd, str, true));

    ASSERT_EQ(o, str);
}

TEST(decompress, decompressZstdMultiFrameLargeInput)
{
    // Create input larger than the 16 MiB frame boundary to exercise
    // multi-frame emission.
    std::string str(20 * 1024 * 1024, 'x');
    for (size_t i = 0; i < str.size(); i += 997)
        str[i] = 'y'; // add some variation
    auto compressed = compress(CompressionAlgo::zstd, str);
    auto o = decompress(CompressionAlgo::zstd, compressed);

    ASSERT_EQ(o, str);
}

TEST(compress, zstdExactFrameBoundary)
{
    // Input exactly equal to the 16 MiB frame boundary should not
    // emit a trailing zero-content frame.
    std::string str(16 * 1024 * 1024, 'z');
    auto compressed = compress(CompressionAlgo::zstd, str);
    auto o = decompress(CompressionAlgo::zstd, compressed);
    ASSERT_EQ(o, str);

    // Verify there is exactly one frame by checking that
    // the first frame's compressed size equals the total size.
    size_t frameSize = ZSTD_findFrameCompressedSize(compressed.data(), compressed.size());
    ASSERT_FALSE(ZSTD_isError(frameSize));
    ASSERT_EQ(frameSize, compressed.size());
}

TEST(decompress, decompressInvalidInputThrowsCompressionError)
{
    auto str = "this is a string that does not qualify as valid bzip2 data";

    ASSERT_THROW(decompress(CompressionAlgo::bzip2, str), CompressionError);
}

/* ----------------------------------------------------------------------------
 * compression sinks
 * --------------------------------------------------------------------------*/

TEST(makeCompressionSink, noneSinkDoesNothingToInput)
{
    StringSink strSink;
    auto inputString = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
    auto sink = makeCompressionSink(CompressionAlgo::none, strSink);
    (*sink)(inputString);
    sink->finish();

    ASSERT_STREQ(strSink.s.c_str(), inputString);
}

} // namespace nix
