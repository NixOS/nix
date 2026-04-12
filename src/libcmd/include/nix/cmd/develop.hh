#include "nix/expr/eval.hh"

namespace nix {

/**
 * Given an existing derivation, return the shell environment as
 * initialised by stdenv's setup script. We do this by building a
 * modified derivation with the same dependencies and nearly the same
 * initial environment variables, that just writes the resulting
 * environment to a file and exits.
 */
StorePath getDerivationEnvironment(ref<Store> store, ref<Store> evalStore, const StorePath & drvPath);

} // namespace nix
