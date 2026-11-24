#pragma once
///@file
///
/// The [version 2 "ATerm" format](@docroot@/protocols/derivation-aterm/v2.md).

#include "nix/store/derivation/v2/derivation.hh"
#include "nix/util/configuration.hh"

namespace nix {

struct StoreDirConfig;

namespace derivation::v2 {

/**
 * Whether `s` is in the version 2 form, rather than version 1.
 */
bool isV2(std::string_view s);

/**
 * A version 2 derivation as read from its encoding, together with the
 * output paths it records.
 *
 * The recorded paths are redundant --- they are what static
 * speculation computes --- so they are not part of `Derivation`, and
 * are checked against `staticOutputPaths` separately.
 */
struct Parsed
{
    Derivation drv;
    std::map<OutputName, StorePath, std::less<>> recordedOutputPaths;
};

/**
 * @param name The derivation name, which is not part of the format.
 */
Parsed parse(
    const StoreDirConfig & store,
    std::string_view s,
    std::string_view name,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * @param outputPaths The paths to record for the input-addressed
 * outputs: exactly those static speculation computes, i.e. all of them
 * or (if deferred) none.
 */
std::string unparse(
    const Derivation & drv,
    const StoreDirConfig & store,
    const std::map<OutputName, StorePath, std::less<>> & outputPaths);

} // namespace derivation::v2

} // namespace nix
