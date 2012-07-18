#pragma once

#include "eval.hh"

#include <string>
#include <map>

namespace nix {

void findAlongAttrPath(EvalState & state, const string & attrPath,
    Bindings & autoArgs, Expr * e, Value & v);

}
