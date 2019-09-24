#include "derivations.hh"
#include "store-api.hh"
#include "types.hh"

namespace nix {

typedef std::map<std::string, std::string> StringRewrites;

std::string rewriteStrings(std::string s, const StringRewrites & rewrites);

/**
 * Replace all occurences of a path in `keys(pathRewrites)` in the derivation
 * by its associated value.
 */
void rewriteDerivation(Store & store, Derivation & drv, const PathMap & pathRewrites);

}
