#pragma once
///@file

#include "nix/store/derivations.hh"
#include "nix/store/derivation-options.hh"
#include "nix/store/path-info.hh"

namespace nix {

/**
 * If outputSpec is a CAFixed output, check that the actual output described in
 * info meets the requirements for a CAFixed output. Do nothing if outputSpec is
 * not a CAFixed output.
 */
void checkCAFixedOutput(
    StoreDirConfig & store, const StorePath & drvPath, const DerivationOutput & outputSpec, const ValidPathInfo & info);

/**
 * Check that outputs meets the requirements specified by the
 * 'outputChecks' attribute (or the legacy
 * '{allowed,disallowed}{References,Requisites}' attributes).
 *
 * The outputs may not be valid yet, hence outputs needs to contain all
 * needed info like the NAR size. However, the external (not other
 * output) references of the output must be valid, so we can compute the
 * closure size.
 */
void checkOutputs(
    Store & store,
    const StorePath & drvPath,
    const decltype(Derivation::outputs) & drvOutputs,
    const decltype(DerivationOptions<StorePath>::outputChecks) & drvOptions,
    const std::map<std::string, ValidPathInfo> & outputs);

} // namespace nix
