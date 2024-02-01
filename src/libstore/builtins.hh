#pragma once
///@file

#include "derivations.hh"

namespace nix {

// TODO: make pluggable.
void builtinFetchurl(
    const BasicDerivation & drv,
    const std::map<std::string, Path> & outputs);

void builtinUnpackChannel(
    const BasicDerivation & drv,
    const std::map<std::string, Path> & outputs);

}
