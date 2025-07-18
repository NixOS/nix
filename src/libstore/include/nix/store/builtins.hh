#pragma once
///@file

#include "nix/store/derivations.hh"

namespace nix {

// TODO: make pluggable.
void builtinFetchurl(
    const BasicDerivation & drv,
    const std::map<std::string, Path> & outputs,
    const std::string & netrcData,
    const std::string & caFileData);

void builtinUnpackChannel(const BasicDerivation & drv, const std::map<std::string, Path> & outputs);

} // namespace nix
