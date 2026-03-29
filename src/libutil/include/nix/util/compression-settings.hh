#pragma once
///@file

#include "nix/util/configuration.hh"
#include "nix/util/compression-algo.hh"

namespace nix {

NIX_DECLARE_CONFIG_SERIALISER(CompressionAlgo)
NIX_DECLARE_CONFIG_SERIALISER(std::optional<CompressionAlgo>)

template<>
struct json_avoids_null<CompressionAlgo> : std::true_type
{};

} // namespace nix
