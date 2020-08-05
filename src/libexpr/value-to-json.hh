#pragma once

#include "nixexpr.hh"
#include "eval.hh"

#include <string>
#include <map>

namespace nix {

nlohmann::json printValueAsJSON(EvalState & state, bool strict,
    Value & v, PathSet & context);

void printValueAsJSON(EvalState & state, bool strict,
    Value & v, std::ostream & str, PathSet & context);

}
