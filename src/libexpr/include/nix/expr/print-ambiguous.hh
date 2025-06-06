#pragma once

#include "nix/expr/value.hh"

namespace nix {

/**
 * Print a value in the deprecated format used by `nix-instantiate --eval` and
 * `nix-env` (for manifests).
 *
 * This output can't be changed because it's part of the `nix-instantiate` API,
 * but it produces ambiguous output; unevaluated thunks and lambdas (and a few
 * other types) are printed as Nix path syntax like `<CODE>`.
 *
 * See: https://github.com/NixOS/nix/issues/9730
 */
void printAmbiguous(
    EvalState & state,
    Value & v,
    std::ostream & str,
    std::set<const void *> * seen,
    int depth);

}
