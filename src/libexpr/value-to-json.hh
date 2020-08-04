#pragma once

#include "nixexpr.hh"
#include "eval.hh"

#include <string>
#include <map>

namespace nix {

/* FIXME just return json value, much easier to use. */

void printValueAsJSON(EvalState & state, bool strict,
    Value & v, nlohmann::json & out, PathSet & context);

void printValueAsJSON(EvalState & state, bool strict,
    Value & v, std::ostream & str, PathSet & context);

}
