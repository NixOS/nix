#pragma once

#include "derivations.hh"
#include "store-api.hh"
#include "types.hh"
#include "path.hh"

namespace nix {

/**
 * Replace all occurences of a path in `keys(pathRewrites)` in the derivation
 * by its associated value.
 */
void rewriteDerivation(Store & store, Derivation & drv, const StringMap & pathRewrites);

/**
 * Replace all the input derivations of the derivation by their output path
 * (as given by `queryDerivationOutputMap`)
 * **if this one differs from the one written in the derivation**
 */
bool resolveDerivation(Store & store, Derivation & drv);

}
