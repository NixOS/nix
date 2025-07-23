#pragma once
///@file

#include "nix/util/configuration.hh"

namespace nix {

enum struct EvalProfilerMode { disabled, flamegraph };

template<>
EvalProfilerMode BaseSetting<EvalProfilerMode>::parse(const std::string & str) const;

template<>
std::string BaseSetting<EvalProfilerMode>::to_string() const;

} // namespace nix
