#pragma once
///@file

#include "nix/store/derivations.hh"

namespace nix {

class Store;

namespace derivation {

/**
 * Determine whether this derivation should be resolved before building.
 *
 * Resolution is needed when:
 * - Input-addressed derivations are deferred (depend on CA derivations)
 * - Content-addressed derivations have input drvs and are either:
 *   - Floating (non-fixed), which must always be resolved
 *   - Fixed, which can optionally be resolved when ca-derivations is enabled
 * - Impure derivations always need resolution
 * - Any input derivations have outputs from dynamic derivations
 */
bool shouldResolve(const Full & drv);

/**
 * Return the underlying basic derivation but with these changes:
 *
 * 1. Input drvs are emptied, but the outputs of them that were used
 *    are added directly to input sources.
 *
 * 2. Input placeholders are replaced with realized input store
 *    paths.
 */
std::optional<Basic> tryResolve(const Full & drv, Store & store, Store * evalStore = nullptr);

/**
 * Like the above, but instead of querying the Nix database for
 * realisations, uses a given mapping from input derivation paths +
 * output names to actual output store paths.
 */
std::optional<Basic> tryResolve(
    const Full & drv,
    Store & store,
    fun<std::optional<StorePath>(ref<const SingleDerivedPath> drvPath, const std::string & outputName)>
        queryResolutionChain);

/**
 * Convert a `Basic` derivation to a `Full` derivation.
 * The resulting derivation has empty input drvs since a `Basic`
 * derivation is already resolved.
 */
Full unresolve(const Basic & drv);

} // namespace derivation

} // namespace nix
