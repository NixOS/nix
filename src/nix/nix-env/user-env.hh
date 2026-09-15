#pragma once
///@file

#include "nix/expr/get-drvs.hh"

namespace nix {

struct Builder;

PackageInfos queryInstalled(EvalState & state, const std::filesystem::path & userEnv);

/**
 * @param builder Used for the builds this takes; pass the one that
 * reported what is missing, so that they share their planning.
 */
bool createUserEnv(
    EvalState & state,
    Builder & builder,
    PackageInfos & elems,
    const std::filesystem::path & profile,
    bool keepDerivations,
    const std::string & lockToken);

} // namespace nix
