#pragma once
///@file
///
/// Version 2 derivations' input addressing, and their conversion to and
/// from the elaborated `derivation::Full` that the rest of Nix works on.

#include "nix/store/derivation/v2/derivation.hh"
#include "nix/store/derivations.hh"
#include "nix/util/repair-flag.hh"

namespace nix::derivation::v2 {

/**
 * The store path of an output of an input derivation, if it is static:
 * known without building anything. See [static
 * speculation](@docroot@/store/derivation/outputs/input-address.md#static).
 */
using StaticPathOf = fun<std::optional<StorePath>(const SingleDerivedPath::Built &)>;

/**
 * [Resolve](@docroot@/store/resolution.md) each input whose store path
 * `pathOf` knows: replace it, wherever it is referred to, by a constant
 * input for that store path.
 *
 * This is the same algorithm whether the store paths come from static
 * speculation or from the build trace; only `pathOf` differs.
 *
 * @return Whether every input was resolved.
 */
bool resolve(Derivation & drv, StaticPathOf staticPathOf);

/**
 * The output paths of an input-addressed derivation, computed by static
 * speculation: from its pre-resolved form.
 *
 * @return `std::nullopt` if some input does not have a static store
 * path, so the output paths are deferred; an empty map if the
 * derivation is not input-addressed.
 */
std::optional<std::map<OutputName, StorePath, std::less<>>>
staticOutputPaths(const StoreDirConfig & store, const Derivation & drv, StaticPathOf staticPathOf);

/**
 * The elaborated form of a version 2 derivation, for building and the
 * other parts of Nix that do not care which version a derivation is.
 *
 * References to inputs with static store paths become those paths, and
 * the other references become (internal) placeholders, as they are for
 * the elaborated form of version 1 derivations.
 *
 * @throws Error if the derivation has options this version of Nix does
 * not know, since then it cannot be built faithfully.
 */
Full toFull(const StoreDirConfig & store, const Derivation & drv, StaticPathOf staticPathOf);

/**
 * The version 2 derivation an elaborated derivation whose `version` is
 * `Version::V2` is the form of: for a resolved derivation, which the
 * build machinery produces in elaborated form, the inverse of `toFull`.
 */
template<typename Input>
Derivation fromElaborated(const StoreDirConfig & store, const derivation::Derivation<Input> & drv);

} // namespace nix::derivation::v2

namespace nix {
class Store;
}

namespace nix::derivation::v2 {

/**
 * `StaticPathOf` for input derivations in `store`.
 */
StaticPathOf staticPathOfStore(Store & store);

/**
 * The store path of a version 2 derivation, whose own recorded output
 * paths are `outputPaths` (see `unparse`).
 */
StorePath computeStorePath(
    const StoreDirConfig & store,
    const Derivation & drv,
    const std::map<OutputName, StorePath, std::less<>> & outputPaths);

/**
 * Write a version 2 derivation to the store, recording the output paths
 * static speculation computes for it.
 */
StorePath writeDerivation(Store & store, const Derivation & drv, RepairFlag repair = NoRepair);

} // namespace nix::derivation::v2
