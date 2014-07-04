#pragma once

#include "eval.hh"

#include <string>

namespace nix {

MakeError(JSONParseError, EvalError)

void parseJSON(EvalState & state, const string & s, Value & v);

}
