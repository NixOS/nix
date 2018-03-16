#pragma once

#include "ref.hh"
#include "types.hh"
#include "serialise.hh"

#include <string>

namespace nix {

ref<std::string> decompress(const std::string & method, const std::string & in);

void decompress(const std::string & method, Source & source, Sink & sink);

ref<std::string> compress(const std::string & method, const std::string & in, const bool parallel = false);

struct CompressionSink : BufferedSink
{
    virtual void finish() = 0;
};

ref<CompressionSink> makeCompressionSink(const std::string & method, Sink & nextSink, const bool parallel = false);

MakeError(UnknownCompressionMethod, Error);

MakeError(CompressionError, Error);

}
