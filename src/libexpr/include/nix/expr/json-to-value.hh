#pragma once

#include "nix/expr/eval.hh"

#include <string>

namespace nix {

MakeError(JSONParseError, EvalError);

void parseJSON(EvalState & state, const std::string_view & s, Value & v);

}
