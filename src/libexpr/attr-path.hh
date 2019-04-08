#pragma once

#include "eval.hh"

#include <string>
#include <map>

namespace nix {

MakeError(AttrPathNotFound, Error);

Value * findAlongAttrPath(EvalState & state, const string & attrPath,
    Bindings & autoArgs, Value & vIn);

}
