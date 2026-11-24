#pragma once
///@file
///
/// The [version 2 "ATerm" format](@docroot@/protocols/derivation-aterm/v2.md).

#include "nix/store/derivation/v2.hh"
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
 * speculation computes --- so they are not part of the derivation, and
 * are checked against `staticOutputPaths` separately.
 *
 * (This nests the other way round from version 1, where the derivation
 * holds what was parsed, plus the name and the options decoded from
 * it. That is not a mistake: it is just how each format happens to
 * relate to the derivations it encodes, not something either is
 * defined by.)
 */
template<typename Input>
struct Parsed
{
    DerivationT<Input> drv;
    std::map<OutputName, StorePath, std::less<>> recordedOutputPaths;
};

/**
 * @tparam Input Which form to read: `SingleDerivedPath` for a full
 * derivation, `StorePath` for a basic one, which has no input
 * derivations and refers to no inputs in its strings.
 *
 * @param name The derivation name, which is not part of the format.
 */
template<typename Input>
Parsed<Input> parse(
    const StoreDirConfig & store,
    std::string_view s,
    std::string_view name,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * @param outputPaths The paths to record for the input-addressed
 * outputs: exactly those static speculation computes, i.e. all of them
 * or (if deferred) none.
 */
template<typename Input>
std::string unparse(
    const DerivationT<Input> & drv,
    const StoreDirConfig & store,
    const std::map<OutputName, StorePath, std::less<>> & outputPaths);

} // namespace derivation::v2

} // namespace nix
