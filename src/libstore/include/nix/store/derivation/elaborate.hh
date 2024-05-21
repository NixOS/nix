#pragma once
///@file

#include "nix/store/derivations.hh"

namespace nix {

/**
 * Parse the legacy environment-variable (and structured-attributes)
 * encodings of the derivation options into the corresponding fields of
 * @a drv: the top-level options, the per-output options, and the
 * per-environment-variable flags.
 *
 * This is for formats that deserialize into a derivation directly
 * (e.g. the wire protocols); nicer formats represent the options
 * first-class instead.
 */
template<typename Input>
void elaborateLegacyOptions(
    const StoreDirConfig & store,
    DerivationT<Input> & drv,
    bool shouldWarn = true,
    const ExperimentalFeatureSettings & mockXpSettings = experimentalFeatureSettings);

/**
 * Resolve the derivation-option fields of @a drv onto @a resolved,
 * mapping any references to derivation outputs to concrete store paths
 * via @a queryResolutionChain.
 *
 * This is the options part of `Derivation::tryResolve`.
 *
 * @return false if some reference could not be resolved.
 */
bool tryResolveDerivationOptions(
    const Derivation & drv,
    BasicDerivation & resolved,
    fun<std::optional<StorePath>(ref<const SingleDerivedPath> drvPath, const std::string & outputName)>
        queryResolutionChain);

} // namespace nix
