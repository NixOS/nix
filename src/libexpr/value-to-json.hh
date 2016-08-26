#pragma once

#include "nixexpr.hh"
#include "eval.hh"

#include <string>
#include <map>

namespace nix {

class JSONPlaceholder;

void printValueAsJSON(EvalState & state, bool strict,
    Value & v, JSONPlaceholder & out, PathSet & context);

void printValueAsJSON(EvalState & state, bool strict,
    Value & v, std::ostream & str, PathSet & context);

}
