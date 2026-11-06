#pragma once
///@file

#include "nix/util/error.hh"

#include <string>
#include <string_view>

namespace nix {

class EvalState;
struct Value;

MakeError(JSONParseError, Error);

void parseJSON(EvalState & state, const std::string_view & s, Value & v);

} // namespace nix
