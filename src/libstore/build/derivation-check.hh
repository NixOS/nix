#pragma once
///@file

#include "nix/store/derivations.hh"
#include "nix/store/derivation-options.hh"
#include "nix/store/path-info.hh"

namespace nix {

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
    const decltype(DerivationOptions::outputChecks) & drvOptions,
    const std::map<std::string, ValidPathInfo> & outputs);

} // namespace nix
