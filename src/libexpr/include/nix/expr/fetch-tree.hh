#pragma once

#include "nix/expr/eval.hh"

namespace nix {

/**
 * Convert a libfetchers `Input` to libexpr `Value`.
 */
void emitTreeAttrs(
    EvalState & state,
    const StorePath & storePath,
    const fetchers::Input & input,
    Value & v,
    bool emptyRevFallback = false,
    bool forceDirty = false);

} // namespace nix
