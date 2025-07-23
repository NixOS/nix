#pragma once
///@file

#include "nix/expr/get-drvs.hh"

namespace nix {

PackageInfos queryInstalled(EvalState & state, const Path & userEnv);

bool createUserEnv(
    EvalState & state, PackageInfos & elems, const Path & profile, bool keepDerivations, const std::string & lockToken);

} // namespace nix
