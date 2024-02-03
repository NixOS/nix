#pragma once
///@file

#include "derivations.hh"

namespace nix {

namespace auth { class Authenticator; }

// TODO: make pluggable.
void builtinFetchurl(
    const BasicDerivation & drv,
    const std::map<std::string, Path> & outputs,
    ref<auth::Authenticator> authenticator);

void builtinUnpackChannel(
    const BasicDerivation & drv,
    const std::map<std::string, Path> & outputs);

}
