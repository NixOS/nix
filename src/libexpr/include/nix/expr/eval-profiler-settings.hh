#pragma once
///@file

#include "nix/util/configuration.hh"

namespace nix {

enum struct EvalProfilerMode { disabled, flamegraph };

NIX_DECLARE_CONFIG_SERIALISER(EvalProfilerMode)

} // namespace nix
