#pragma once

#include "ref.hh"
#include "types.hh"
#include "serialise.hh"

#include <string>

namespace nix {

ref<std::string> compress(const std::string & method, const std::string & in);

ref<std::string> decompress(const std::string & method, const std::string & in);

struct CompressionSink : BufferedSink
{
    virtual void finish() = 0;
};

ref<CompressionSink> makeCompressionSink(const std::string & method, Sink & nextSink);

MakeError(UnknownCompressionMethod, Error);

}
