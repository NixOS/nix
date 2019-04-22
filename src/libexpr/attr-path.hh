#pragma once

#include "eval.hh"

#include <string>
#include <map>

namespace nix {

Ptr<Value> findAlongAttrPath(EvalState & state, const string & attrPath,
    Bindings & autoArgs, Value & vIn);

}
