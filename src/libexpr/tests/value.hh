#pragma once
///@file

#include <rapidcheck/gen/Arbitrary.h>

#include <eval.hh>
#include <value.hh>

namespace rc {
using namespace nix;

Gen<Value> genTOMLSerializableNixValue(EvalState & state);

} // namespace rc
