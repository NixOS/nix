#pragma once
///@file

#include "nix/expr/nixexpr.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/value/context.hh"
#include "nix/util/pos-idx.hh"

#include <string>
#include <map>
#include <ostream>

namespace nix {

void printValueAsXML(
    EvalState & state,
    bool strict,
    bool location,
    Value & v,
    std::ostream & out,
    NixStringContext & context,
    const PosIdx pos);

}
