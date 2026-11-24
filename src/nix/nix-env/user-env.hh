#pragma once
///@file

#include "nix/expr/get-drvs.hh"

namespace nix {

/**
 * The packages installed in the user environment @p userEnv.
 */
PackageInfos queryInstalled(EvalState & state, const SourcePath & userEnv);

bool createUserEnv(
    EvalState & state,
    PackageInfos & elems,
    const std::filesystem::path & profile,
    bool keepDerivations,
    const std::string & lockToken);

} // namespace nix
