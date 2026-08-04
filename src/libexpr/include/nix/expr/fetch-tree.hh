#pragma once

#include "nix/expr/eval.hh"

namespace nix {

/**
 * Convert a libfetchers `Input` to libexpr `Value`.
 *
 * @param `callPos` optional position of the `fetchTree` / `fetchGit` / ... call
 *   that produces this attrset, added to every attribute of the returned
 *   attrset as its `Attr::pos` for diagnostics and `unsafeGetAttrPos`.
 */
void emitTreeAttrs(
    EvalState & state,
    PosIdx callPos,
    const StorePath & storePath,
    const fetchers::Input & input,
    Value & v,
    bool emptyRevFallback = false,
    bool forceDirty = false);

} // namespace nix
