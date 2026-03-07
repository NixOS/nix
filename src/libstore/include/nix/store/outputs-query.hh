#pragma once
///@file

#include "nix/store/store-api.hh"

#include <functional>

namespace nix {

/**
 * Callback type for querying realisations. The callback should return
 * the realisation for the given DrvOutput, or nullptr if not found.
 */
using QueryRealisationFun = std::function<std::shared_ptr<const UnkeyedRealisation>(const DrvOutput &)>;

/**
 * For internal use only.
 */
void queryPartialDerivationOutputMapCA(
    Store & store,
    const StorePath & drvPath,
    const BasicDerivation & drv,
    std::map<std::string, std::optional<StorePath>> & outputs,
    QueryRealisationFun queryRealisation = {});

struct DeepDerivationOutputResult
{
    /**
     * The output path, if known.
     */
    std::optional<StorePath> outPath;

    /**
     * The resolved derivation path. For non-CA derivations or
     * derivations that don't need resolution, this equals the
     * original drvPath.
     */
    StorePath resolvedDrvPath;
};

/**
 * Like Store::queryStaticPartialDerivationOutput, but resolves the
 * derivation first if needed. Returns both the output path and
 * the resolved derivation path.
 *
 * @param queryRealisation Optional callback for querying realisations.
 *        If not provided, uses store.queryRealisation().
 */
DeepDerivationOutputResult deepQueryPartialDerivationOutput(
    Store & store,
    const StorePath & drvPath,
    const std::string & outputName,
    Store * evalStore = nullptr,
    QueryRealisationFun queryRealisation = {});

/**
 * Like Store::queryStaticPartialDerivationOutputMap, but resolves the
 * derivation first if needed for CA derivation output lookup.
 *
 * @param queryRealisation Optional callback for querying realisations.
 *        If not provided, uses store.queryRealisation().
 */
std::map<std::string, std::optional<StorePath>> deepQueryPartialDerivationOutputMap(
    Store & store, const StorePath & drvPath, Store * evalStore = nullptr, QueryRealisationFun queryRealisation = {});

/**
 * Like deepQueryPartialDerivationOutputMap, but throws if any output
 * path is not known.
 *
 * @param queryRealisation Optional callback for querying realisations.
 *        If not provided, uses store.queryRealisation().
 */
OutputPathMap deepQueryDerivationOutputMap(
    Store & store, const StorePath & drvPath, Store * evalStore = nullptr, QueryRealisationFun queryRealisation = {});

} // namespace nix
