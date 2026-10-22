#include "nix/util/compression.hh"
#include "nix/util/error.hh"
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
    if (GetParam() != CompressionAlgo::none) {
        ASSERT_FALSE(compressed.empty());
    }
    auto o = decompress(GetParam(), compressed);
    ASSERT_EQ(o, "");
}

TEST_P(CompressionDecompressionTest, invalidDecompression)
{
    if (GetParam() == CompressionAlgo::none)
        GTEST_SKIP();

    ASSERT_THROW(
        decompress(GetParam(), "this is a string that does not qualify as valid compressed data"), CompressionError);
}

/* Completely empty input is treated as an empty stream rather than a
   truncated one, for every algorithm. */
TEST_P(CompressionDecompressionTest, emptyInputDecompressesToEmpty)
{
    ASSERT_EQ(decompress(GetParam(), ""), "");

    StringSink output;
    auto sink = makeDecompressionSink(GetParam(), output);
    sink->finish();
    ASSERT_EQ(output.s, "");
}

TEST_P(CompressionDecompressionTest, truncatedInputIsRejected)
{
    if (GetParam() == CompressionAlgo::none)
        GTEST_SKIP();

    auto compressed = compress(GetParam(), "hello world");
    for (size_t size : {size_t{1}, compressed.size() / 2, compressed.size() - 1}) {
        SCOPED_TRACE(size);
        auto truncated = std::string_view(compressed).substr(0, size);
        ASSERT_THROW(decompress(GetParam(), truncated), Error);

        StringSink output;
        auto sink = makeDecompressionSink(GetParam(), output);
        ASSERT_THROW(
            {
                for (size_t i = 0; i < truncated.size(); ++i)
                    (*sink)(truncated.substr(i, 1));
                sink->finish();
            },
            Error);
    }
}

TEST(decompress, brotliWaitsForInputUntilFinish)
{
    const std::string input = "hello world";
    auto compressed = compress(CompressionAlgo::brotli, input);
    for (size_t size = 1; size <= compressed.size(); ++size) {
        SCOPED_TRACE(size);
        StringSink output;
        auto sink = makeDecompressionSink(CompressionAlgo::brotli, output);
        auto buffered = dynamic_cast<BufferedSink *>(sink.get());
        ASSERT_NE(buffered, nullptr);
        for (size_t i = 0; i < size; ++i) {
            ASSERT_NO_THROW({
                (*sink)(std::string_view(compressed).substr(i, 1));
                // Deliver each byte to the decoder instead of buffering it until finish().
                buffered->flush();
            });
        }
        if (size < compressed.size()) {
            ASSERT_THROW(sink->finish(), CompressionError);
        } else {
            ASSERT_NO_THROW(sink->finish());
            ASSERT_EQ(output.s, input);
        }
    }
}

TEST_P(CompressionDecompressionTest, roundtripsWithSourceAndSink)
{
    StringSink strSink;
    auto decompressionSink = makeDecompressionSink(GetParam(), strSink);
    auto sink = makeCompressionSink(GetParam(), *decompressionSink);

    (*sink)(dummyInput);
    sink->finish();
    decompressionSink->finish();

    ASSERT_EQ(strSink.s.c_str(), dummyInput);
}

TEST(DecompressionSink, abandonCoroutine)
{
    StringSink strSink;
    auto decompressionSink = makeDecompressionSink(CompressionAlgo::bzip2, strSink);
    (*decompressionSink)("BZh");
    /* Abandon the coroutine. It will be unwound via forced_unwind. */
    decompressionSink.reset();
}

TEST_P(CompressionDecompressionTest, failingSink)
{
    /* Like a full disk: every write fails, including the ones made while the compression sink is being destroyed. */
    struct FailingSink : Sink
    {
        void operator()(std::string_view data) override
        {
            throw SysError(ENOSPC, "write of %d bytes", data.size());
        }
    };

    /* Make the input incompressible enough to produce output before finish(). */
    std::string input(1024 * 1024, 0);
    uint32_t x = 1;
    for (auto & c : input) {
        x = x * 1664525 + 1013904223;
        c = x >> 24;
    }

    FailingSink failingSink;
    ASSERT_THROW(
        {
            auto sink = makeCompressionSink(GetParam(), failingSink);
            (*sink)(input);
            sink->finish();
        },
        SysError);
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
