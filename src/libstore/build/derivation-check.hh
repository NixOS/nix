#pragma once
///@file

#include "nix/store/derivations.hh"
#include "nix/store/derivation/elaborate.hh"
#include "nix/store/path-info.hh"

namespace nix {

/**
 * If outputSpec is a CAFixed or CAFloating output, check that the actual output described in
 * info meets the requirements for a CA output.
 * Do nothing if outputSpec is not a CAFixed or CAFloating output.
 */
void checkCAOutput(
    StoreDirConfig & store,
    const StorePath & drvPath,
    const DerivationOutput & outputSpec,
    const ValidPathInfo & info,
    const std::string & outputName);

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
    const BasicDerivation & drv,
    const std::map<std::string, ValidPathInfo> & outputs);

} // namespace nix
