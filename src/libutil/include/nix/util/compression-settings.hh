#pragma once
///@file

#include "nix/util/configuration.hh"
#include "nix/util/compression-algo.hh"

namespace nix {

template<>
CompressionAlgo BaseSetting<CompressionAlgo>::parse(const std::string & str) const;

template<>
std::string BaseSetting<CompressionAlgo>::to_string() const;

template<>
std::optional<CompressionAlgo> BaseSetting<std::optional<CompressionAlgo>>::parse(const std::string & str) const;

template<>
std::string BaseSetting<std::optional<CompressionAlgo>>::to_string() const;

} // namespace nix
