#pragma once

#include "eval.hh"

#include <string>
#include <map>

namespace nix {

MakeError(AttrPathNotFound, Error);
MakeError(NoPositionInfo, Error);

std::pair<Value *, PosIdx> findAlongAttrPath(
    EvalState & state,
    const std::string & attrPath,
    Bindings & autoArgs,
    Value & vIn);

/* Heuristic to find the filename and lineno or a nix value. */
std::pair<SourcePath, uint32_t> findPackageFilename(EvalState & state, Value & v, std::string what);

std::vector<Symbol> parseAttrPath(EvalState & state, std::string_view s);

}
