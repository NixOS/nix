#pragma once
///@file

#include "nix/store/derivations.hh"
#include "nix/store/derivation/traditional.hh"

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

namespace nix::derivation {

/**
 * Decode the options and structured attributes a traditional-shape
 * derivation encodes in its environment, producing the shared view
 * directly.
 *
 * This is the decoding the version 1 derivation types (see
 * `nix/store/derivation/v1.hh`) are built on; everything else should go
 * through them, or `elaborate`, instead.
 */
Full decodeTraditional(
    const TraditionalParsed & aterm,
    const StoreDirConfig & store,
    std::string_view name,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);
Basic decodeTraditional(
    const BasicTraditionalParsed & aterm,
    const StoreDirConfig & store,
    std::string_view name,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

} // namespace nix::derivation
