#pragma once

#include "derivations.hh"

namespace nix {

void builtinFetchurl(const BasicDerivation & drv, const std::string & netrcData);

}
