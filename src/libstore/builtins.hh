#pragma once
///@file

#include "derivations.hh"

namespace nix {

// TODO: make pluggable.
void builtinFetchurl(const BasicDerivation & drv,
    const std::string & netrcData,
    const std::string & caFileData);
void builtinUnpackChannel(const BasicDerivation & drv);

}
