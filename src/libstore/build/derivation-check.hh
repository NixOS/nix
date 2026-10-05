#pragma once
///@file

#include "nix/store/derivations.hh"
#include "nix/store/derivation-options.hh"
#include "nix/store/path-info.hh"

namespace nix {

/**
 * If outputSpec is a CAFixed or CAFloating output, check that the actual output described in
 * info meets the requirements for a CA output.
 * Do nothing if outputSpec is not a CAFixed or CAFloating output.
 */
void checkCAOutput(
    const StoreDirConfig & store,
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
 *
 * @param outputPaths Supplementary name -> final store path map,
 * consulted only as a fallback for symbolic output-name references
 * (e.g. `"out"`) that `outputChecks` uses to refer to sibling outputs
 * and that are not found in `outputs`. Its purpose is to cover
 * outputs that were already valid before this build and hence are
 * absent from `outputs`: such a sibling is a legitimate reference
 * target even though it is not itself being (re-)checked here. It
 * may be incomplete or empty — under `bmCheck`, for instance,
 * `outputs` already covers every output and the fallback is never
 * reached — so it must not be treated as an exhaustive output list.
 */
void checkOutputs(
    Store & store,
    const StorePath & drvPath,
    const BasicDerivation & drv,
    const decltype(DerivationOptions<StorePath>::outputChecks) & drvOptions,
    const std::map<std::string, ValidPathInfo> & outputs,
    const std::map<std::string, StorePath> & outputPaths);

} // namespace nix
