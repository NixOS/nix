#pragma once
///@file

#include "nix/util/ref.hh"
#include "nix/util/types.hh"
#include "nix/util/serialise.hh"

#include <string>

namespace nix {

struct CompressionSink : BufferedSink, FinishSink
{
    using BufferedSink::operator();
    using BufferedSink::writeUnbuffered;
    using FinishSink::finish;
};

enum class CompressionAlgo {
    none,
    brotli,
    bzip2,
    compress,
    grzip,
    gzip,
    lrzip,
    lz4,
    lzip,
    lzma,
    lzop,
    xz,
    zstd,
};

std::string decompress(const std::string & method, std::string_view in);

std::unique_ptr<FinishSink> makeDecompressionSink(const std::string & method, Sink & nextSink);

std::string compress(const std::string & method, std::string_view in, const bool parallel = false, int level = -1);

ref<CompressionSink>
makeCompressionSink(const std::string & method, Sink & nextSink, const bool parallel = false, int level = -1);

ref<CompressionSink>
makeCompressionSink(CompressionAlgo method, Sink & nextSink, const bool parallel = false, int level = -1);

MakeError(UnknownCompressionMethod, Error);

MakeError(CompressionError, Error);

} // namespace nix
