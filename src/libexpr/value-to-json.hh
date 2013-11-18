#pragma once

#include "nixexpr.hh"
#include "eval.hh"

#include <string>
#include <map>

namespace nix {

void printValueAsJSON(EvalState & state, bool strict,
    Value & v, std::ostream & out, PathSet & context);

}
