#pragma once

#include "nix/store/store-api.hh"

namespace nix {

/**
 * Export multiple paths in the format expected by `nix-store
 * --import`. The paths will be sorted topologically.
 */
void exportPaths(Store & store, const StorePathSet & paths, Sink & sink, unsigned int version);

/**
 * Import a sequence of NAR dumps created by `exportPaths()` into the
 * Nix store.
 */
StorePaths importPaths(Store & store, Source & source, CheckSigsFlag checkSigs = CheckSigs);

} // namespace nix
