#pragma once
///@file

#include "nix/expr/get-drvs.hh"

namespace nix {

PackageInfos queryInstalled(EvalState & state, const std::filesystem::path & userEnv);

bool createUserEnv(
    EvalState & state,
    PackageInfos & elems,
    const std::filesystem::path & profile,
    bool keepDerivations,
    const std::string & lockToken);

} // namespace nix
