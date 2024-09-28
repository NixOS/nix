#pragma once
///@file

#include "derivations.hh"

namespace nix {

// TODO: make pluggable.
<<<<<<< HEAD
void builtinFetchurl(const BasicDerivation & drv, const std::string & netrcData);
void builtinUnpackChannel(const BasicDerivation & drv);
=======
void builtinFetchurl(
    const BasicDerivation & drv,
    const std::map<std::string, Path> & outputs,
    const std::string & netrcData,
    const std::string & caFileData);

void builtinUnpackChannel(
    const BasicDerivation & drv,
    const std::map<std::string, Path> & outputs);
>>>>>>> c1ecf0bee (fix passing CA files into builtins:fetchurl sandbox)

}
