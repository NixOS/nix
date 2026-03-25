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

TEST(decompress, decompressNoneCompressed)
{
    auto method = "none";
    auto str = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
    auto o = decompress(method, str);

    ASSERT_EQ(o, str);
}

TEST(decompress, decompressEmptyCompressed)
{
    // Empty-method decompression used e.g. by S3 store
    // (Content-Encoding == "").
    auto method = "";
    auto str = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
    auto o = decompress(method, str);

    ASSERT_EQ(o, str);
}

TEST(decompress, decompressXzCompressed)
{
    auto method = "xz";
    auto str = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
    auto o = decompress(method, compress(CompressionAlgo::xz, str));

    ASSERT_EQ(o, str);
}

TEST(decompress, decompressBzip2Compressed)
{
    auto method = "bzip2";
    auto str = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
    auto o = decompress(method, compress(CompressionAlgo::bzip2, str));

    ASSERT_EQ(o, str);
}

TEST(decompress, decompressBrCompressed)
{
    auto method = "br";
    auto str = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
    auto o = decompress(method, compress(CompressionAlgo::brotli, str));

    ASSERT_EQ(o, str);
}

TEST(decompress, decompressZstdCompressed)
{
    auto method = "zstd";
    auto str = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
    auto o = decompress(method, compress(CompressionAlgo::zstd, str));

    ASSERT_EQ(o, str);
}

TEST(decompress, decompressZstdCompressedParallel)
{
    auto method = "zstd";
    auto str = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
    auto o = decompress(method, compress(CompressionAlgo::zstd, str, true));

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
    auto o = decompress("zstd", compressed);

    ASSERT_EQ(o, str);
}

TEST(compress, zstdEmptyInput)
{
    auto compressed = compress(CompressionAlgo::zstd, "");
    // Empty input should still emit a valid (empty-content) zstd
    // frame so it round-trips through the decompressor — matching
    // the previous libarchive-based sink's behaviour.
    ASSERT_FALSE(compressed.empty());
    auto o = decompress("zstd", compressed);
    ASSERT_EQ(o, "");
}

TEST(compress, zstdExactFrameBoundary)
{
    // Input exactly equal to the 16 MiB frame boundary should not
    // emit a trailing zero-content frame.
    std::string str(16 * 1024 * 1024, 'z');
    auto compressed = compress(CompressionAlgo::zstd, str);
    auto o = decompress("zstd", compressed);
    ASSERT_EQ(o, str);

    // Verify there is exactly one frame by checking that
    // the first frame's compressed size equals the total size.
    size_t frameSize = ZSTD_findFrameCompressedSize(compressed.data(), compressed.size());
    ASSERT_FALSE(ZSTD_isError(frameSize));
    ASSERT_EQ(frameSize, compressed.size());
}

TEST(decompress, decompressInvalidInputThrowsCompressionError)
{
    auto method = "bzip2";
    auto str = "this is a string that does not qualify as valid bzip2 data";

    ASSERT_THROW(decompress(method, str), CompressionError);
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

TEST(makeCompressionSink, compressAndDecompress)
{
    StringSink strSink;
    auto inputString = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
    auto decompressionSink = makeDecompressionSink("bzip2", strSink);
    auto sink = makeCompressionSink(CompressionAlgo::bzip2, *decompressionSink);

    (*sink)(inputString);
    sink->finish();
    decompressionSink->finish();

    ASSERT_STREQ(strSink.s.c_str(), inputString);
}

} // namespace nix
