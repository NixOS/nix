#pragma once
///@file

#include "nix/expr/eval.hh"

#include <string>
#include <map>

namespace nix {

MakeError(AttrPathNotFound, Error);
MakeError(NoPositionInfo, Error);

std::pair<Value *, PosIdx>
findAlongAttrPath(EvalState & state, const std::string & attrPath, Bindings & autoArgs, Value & vIn);

/**
 * Heuristic to find the filename and lineno or a nix value.
 */
std::pair<SourcePath, uint32_t> findPackageFilename(EvalState & state, Value & v, std::string what);

struct AttrPath : std::vector<Symbol>
{
    using std::vector<Symbol>::vector;

    static AttrPath parse(EvalState & state, std::string_view s);

    std::string to_string(EvalState & state) const;

    std::vector<SymbolStr> resolve(EvalState & state) const;
};

} // namespace nix
