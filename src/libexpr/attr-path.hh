#pragma once

#include "eval.hh"

#include <string>
#include <map>

namespace nix {

MakeError(AttrPathNotFound, Error);
MakeError(NoPositionInfo, Error);

std::pair<Value *, Pos> findAlongAttrPath(EvalState & state, const string & attrPath,
    Bindings & autoArgs, Value & vIn);

/* Heuristic to find the filename and lineno or a nix value. */
Pos findDerivationFilename(EvalState & state, Value & v, std::string what);

}
