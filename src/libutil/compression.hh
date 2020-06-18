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

ref<std::string> decompress(const std::string & method, const std::string & in);

ref<CompressionSink> makeDecompressionSink(const std::string & method, Sink & nextSink);

ref<std::string> compress(const std::string & method, const std::string & in, const bool parallel = false);

ref<CompressionSink> makeCompressionSink(const std::string & method, Sink & nextSink, const bool parallel = false);

MakeError(UnknownCompressionMethod, Error);

MakeError(CompressionError, Error);

template<>
struct TeeSink<ref<CompressionSink>> : CompressionSink
{
    MAKE_TEE_SINK(ref<CompressionSink>);
    void finish() override {
        orig->finish();
    }
    void write(const unsigned char * data, size_t len) override {
        return orig->write(data, len);
    }
};

}
