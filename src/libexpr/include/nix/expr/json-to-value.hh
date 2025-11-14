#pragma once
///@file

#include "nix/util/error.hh"

#include <string>

namespace nix {

class EvalState;
struct Value;

MakeError(JSONParseError, Error);

/**
 * Parse the string into the Value
 * @param state
 * @param s
 * @param v
 * @param allowComments Whether to allow comments in the JSON
 */
void parseJSON(EvalState & state, const std::string_view & s, Value & v, bool allowComments = false);

} // namespace nix
