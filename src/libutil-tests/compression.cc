#include "nix/util/compression.hh"
#include <gtest/gtest.h>

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

    auto method = CompressionAlgo::none;
    auto str = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
    auto o = decompress(method, str);

    ASSERT_EQ(o, str);
}

TEST(decompress, decompressEmptyCompressed)
{
    // Empty-method decompression used e.g. by S3 store
    // (Content-Encoding == "").
    auto method = CompressionAlgo::none; // Do we handle this in S3 store???
    auto str = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
    auto o = decompress(method, str);

    ASSERT_EQ(o, str);
}

TEST(decompress, decompressXzCompressed)
{
    auto method = CompressionAlgo::xz;
    auto str = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
    auto o = decompress(method, compress(CompressionAlgo::xz, str));

    ASSERT_EQ(o, str);
}

TEST(decompress, decompressBzip2Compressed)
{
    auto method = CompressionAlgo::bzip2;
    auto str = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
    auto o = decompress(method, compress(CompressionAlgo::bzip2, str));

    ASSERT_EQ(o, str);
}

TEST(decompress, decompressBrCompressed)
{
    auto method = CompressionAlgo::brotli;
    auto str = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
    auto o = decompress(method, compress(CompressionAlgo::brotli, str));

    ASSERT_EQ(o, str);
}

TEST(decompress, decompressInvalidInputThrowsCompressionError)
{
    auto method = CompressionAlgo::bzip2;
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
    auto decompressionSink = makeDecompressionSink(CompressionAlgo::bzip2, strSink);
    auto sink = makeCompressionSink(CompressionAlgo::bzip2, *decompressionSink);

    (*sink)(inputString);
    sink->finish();
    decompressionSink->finish();

    ASSERT_STREQ(strSink.s.c_str(), inputString);
}

} // namespace nix
