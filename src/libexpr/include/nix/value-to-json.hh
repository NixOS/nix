#pragma once
///@file

#include "nix/nixexpr.hh"
#include "nix/eval.hh"

#include <string>
#include <map>
#include <nlohmann/json_fwd.hpp>

namespace nix {

nlohmann::json printValueAsJSON(EvalState & state, bool strict,
    Value & v, const PosIdx pos, NixStringContext & context, bool copyToStore = true);

void printValueAsJSON(EvalState & state, bool strict,
    Value & v, const PosIdx pos, std::ostream & str, NixStringContext & context, bool copyToStore = true);


MakeError(JSONSerializationError, Error);

}
