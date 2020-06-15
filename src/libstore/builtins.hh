#pragma once

#include "derivations.hh"

namespace nix {

// TODO: make pluggable.
void builtinFetchurl(const BasicDerivation & drv, std::string_view netrcData);
void builtinUnpackChannel(const BasicDerivation & drv);

}
