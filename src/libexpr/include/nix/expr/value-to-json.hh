#pragma once
///@file

#include "nix/expr/nixexpr.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/value/context.hh"
#include "nix/util/error.hh"
#include "nix/util/pos-idx.hh"

#include <string>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <ostream>

namespace nix {

nlohmann::json printValueAsJSON(
    EvalState & state, bool strict, Value & v, const PosIdx pos, NixStringContext & context, bool copyToStore = true);

void printValueAsJSON(
    EvalState & state,
    bool strict,
    Value & v,
    const PosIdx pos,
    std::ostream & str,
    NixStringContext & context,
    bool copyToStore = true);

MakeError(JSONSerializationError, Error);

} // namespace nix
