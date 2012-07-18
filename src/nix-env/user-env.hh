#pragma once

#include "get-drvs.hh"

namespace nix {

DrvInfos queryInstalled(EvalState & state, const Path & userEnv);

bool createUserEnv(EvalState & state, DrvInfos & elems,
    const Path & profile, bool keepDerivations,
    const string & lockToken);

}
