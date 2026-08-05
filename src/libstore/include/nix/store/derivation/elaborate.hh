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
    derivation::Derivation<Input> & drv,
    bool shouldWarn = true,
    const ExperimentalFeatureSettings & mockXpSettings = experimentalFeatureSettings);

} // namespace nix
