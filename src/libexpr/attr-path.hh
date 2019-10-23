#pragma once

#include "eval.hh"

#include <string>
#include <map>
#include <tuple>

namespace nix {

Value * findAlongAttrPath(EvalState & state, const string & attrPath,
    Bindings & autoArgs, Value & vIn);

/* Heuristic to find the filename and lineno or a derivation. */
std::tuple<std::string, int> findDerivationFilename(EvalState & state,
    Value & v, std::string what);

}
