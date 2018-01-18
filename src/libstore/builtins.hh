#pragma once

#include "derivations.hh"

namespace nix {

void builtinFetchurl(const BasicDerivation & drv, const std::string & netrcData, std::function<std::string(const std::string &)> rewriteStrings);

}
