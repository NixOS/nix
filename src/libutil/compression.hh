#pragma once

#include "ref.hh"
#include "types.hh"
#include "serialise.hh"

#include <string>

namespace nix {

struct CompressionSink : BufferedSink
{
    virtual void finish() = 0;
};

ref<std::string> decompress(std::string_view method, std::string_view in);

ref<CompressionSink> makeDecompressionSink(std::string_view method, Sink & nextSink);

ref<std::string> compress(std::string_view method, std::string_view in, const bool parallel = false);

ref<CompressionSink> makeCompressionSink(std::string_view method, Sink & nextSink, const bool parallel = false);

MakeError(UnknownCompressionMethod, Error);

MakeError(CompressionError, Error);

}
