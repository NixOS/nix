#pragma once

#include "nix/expr/nixexpr.hh"
#include "nix/expr/eval.hh"

#include <string>
#include <map>

namespace nix {

void printValueAsXML(EvalState & state, bool strict, bool location,
    Value & v, std::ostream & out, PathSet & context, const PosIdx pos);

}
