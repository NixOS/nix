#pragma once

#include "get-drvs.hh"

namespace nix {

DrvInfos queryInstalled(EvalState & state, PathView userEnv);

bool createUserEnv(EvalState & state, DrvInfos & elems,
    PathView profile, bool keepDerivations,
    std::string_view lockToken);

}
