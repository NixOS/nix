#pragma once
///@file

#include "nix/store/derivation-options.hh"

namespace nix {

/**
 * Parse this information from its legacy encoding as part of the
 * environment. This should not be used with nice greenfield formats
 * (e.g. JSON) but is necessary for supporting old formats (e.g.
 * ATerm).
 */
DerivationOptions<SingleDerivedPath> derivationOptionsFromStructuredAttrs(
    const StoreDirConfig & store,
    const std::set<SingleDerivedPath> & inputs,
    const StringMap & env,
    const StructuredAttrs * parsed,
    bool shouldWarn = true,
    const ExperimentalFeatureSettings & mockXpSettings = experimentalFeatureSettings);

DerivationOptions<StorePath> derivationOptionsFromStructuredAttrs(
    const StoreDirConfig & store,
    const StringMap & env,
    const StructuredAttrs * parsed,
    bool shouldWarn = true,
    const ExperimentalFeatureSettings & mockXpSettings = experimentalFeatureSettings);

/**
 * This is the counterpart of `Derivation::tryResolve`. In particular,
 * it takes the same sort of callback, which is used to reolve
 * non-constant deriving paths.
 *
 * We need this function when resolving a derivation, and we will use
 * this as part of that if/when `Derivation` includes
 * `DerivationOptions`
 */
std::optional<DerivationOptions<StorePath>> tryResolve(
    const DerivationOptions<SingleDerivedPath> & drvOptions,
    fun<std::optional<StorePath>(ref<const SingleDerivedPath> drvPath, const std::string & outputName)>
        queryResolutionChain);

} // namespace nix
