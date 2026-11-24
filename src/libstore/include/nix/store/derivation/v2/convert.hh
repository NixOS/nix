#pragma once
///@file
///
/// Version 2 derivations' resolution and input addressing, and their
/// conversion to and from the elaborated `derivation::Full` /
/// `derivation::Basic` that the rest of Nix works on.

#include "nix/store/derivation/v2.hh"
#include "nix/store/derivations.hh"
#include "nix/util/repair-flag.hh"

namespace nix {
class Store;
}

namespace nix::derivation::v2 {

/**
 * The store path of an output of an input derivation, if it is known.
 */
using PathOf = fun<std::optional<StorePath>(const SingleDerivedPath::Built &)>;

/**
 * [Resolve](@docroot@/store/resolution.md) a derivation: replace each
 * input deriving path by the store path it denotes, wherever it is
 * referred to.
 *
 * This is the same algorithm whether the store paths come from static
 * speculation or from the build trace; only `pathOf` differs.
 *
 * @return `std::nullopt` if `pathOf` does not know some input's store
 * path.
 */
std::optional<Basic> resolve(const StoreDirConfig & store, const Full & drv, PathOf pathOf);

/**
 * The store path of an output of an input derivation, if it is static:
 * known without building anything. See [static
 * speculation](@docroot@/store/derivation/outputs/input-address.md#static).
 */
PathOf staticPathOf(Store & store);

/**
 * The output paths of an input-addressed basic derivation.
 *
 * @return an empty map if it is not input-addressed.
 */
std::map<OutputName, StorePath, std::less<>> outputPaths(const StoreDirConfig & store, const Basic & drv);

/**
 * The output paths of an input-addressed derivation, computed by static
 * speculation: from its pre-resolved form.
 *
 * @return `std::nullopt` if some input does not have a static store
 * path, so the output paths are deferred; an empty map if the
 * derivation is not input-addressed.
 */
std::optional<std::map<OutputName, StorePath, std::less<>>>
staticOutputPaths(const StoreDirConfig & store, const Full & drv, PathOf staticPathOf);

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
derivation::Full toElaborated(const StoreDirConfig & store, const Full & drv, PathOf staticPathOf);

/**
 * The basic version 2 derivation that an elaborated derivation whose
 * `version` is `Version::V2` is the form of. Only resolved derivations
 * are converted back, as those are what the build machinery produces.
 *
 * @throws Error if `drv` has an input that is not a constant.
 */
template<typename Input>
Basic fromElaborated(const StoreDirConfig & store, const derivation::Derivation<Input> & drv);

/**
 * Write a full version 2 derivation to the store, recording the output
 * paths static speculation computes for it.
 */
StorePath writeDerivation(Store & store, const Full & drv, RepairFlag repair = NoRepair);

/**
 * The store path a full version 2 derivation would be written to.
 */
StorePath computeStorePath(Store & store, const Full & drv);

} // namespace nix::derivation::v2
