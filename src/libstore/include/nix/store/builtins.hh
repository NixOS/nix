#pragma once

#include "nix/store/derivations.hh"

namespace nix {

// TODO: make pluggable.
void builtinFetchurl(const BasicDerivation & drv, const std::string & netrcData);
void builtinUnpackChannel(const BasicDerivation & drv);

}
