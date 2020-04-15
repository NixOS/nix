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

}
