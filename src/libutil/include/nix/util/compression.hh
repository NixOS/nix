#pragma once
///@file

#include "nix/util/ref.hh"
#include "nix/util/types.hh"
#include "nix/util/serialise.hh"
#include "nix/util/compression-algo.hh"

#include <string>

namespace nix {

class CompressionSink : public BufferedSink, public FinishSink
{
    void anchor() override;

public:
    using BufferedSink::operator();
    using BufferedSink::writeUnbuffered;
    using FinishSink::finish;
};

std::string decompress(CompressionAlgo method, std::string_view in);

std::unique_ptr<FinishSink> makeDecompressionSink(CompressionAlgo method, Sink & nextSink);

std::string compress(CompressionAlgo method, std::string_view in, const bool parallel = false, int level = -1);

std::string compress(CompressionAlgo method, Source & in, const bool parallel = false, int level = -1);

ref<CompressionSink>
makeCompressionSink(CompressionAlgo method, Sink & nextSink, const bool parallel = false, int level = -1);

MakeError(CompressionError, Error);

} // namespace nix
