#pragma once

#include "nixexpr.hh"
#include "eval.hh"

#include <string>
#include <map>
#include <nlohmann/json_fwd.hpp>

namespace nix {

nlohmann::json printValueAsJSON(EvalState & state, bool strict,
    Value & v, const PosIdx pos, PathSet & context, bool copyToStore = true);

void printValueAsJSON(EvalState & state, bool strict,
    Value & v, const PosIdx pos, std::ostream & str, PathSet & context, bool copyToStore = true);

}
