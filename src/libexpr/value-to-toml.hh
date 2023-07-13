#pragma once
///@file

#include "nixexpr.hh"
#include "eval.hh"

#include "../toml11/toml/types.hpp"

#include <string>
#include <map>

namespace nix {

toml::basic_value<toml::discard_comments, std::map, std::vector> printValueAsTOML(EvalState & state, bool strict,
    Value & v, const PosIdx pos, NixStringContext & context, bool copyToStore = true);

void printValueAsTOML(EvalState & state, bool strict,
    Value & v, const PosIdx pos, std::ostream & str, NixStringContext & context, bool copyToStore = true);

}
