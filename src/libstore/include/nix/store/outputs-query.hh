#pragma once
///@file

#include "nix/store/store-api.hh"

#include <functional>

namespace nix {

/**
 * Callback type for querying realisations. The callback should return
 * the realisation for the given `DrvOutput`, or `nullptr` if not found.
 */
using QueryRealisationFun = std::function<std::shared_ptr<const UnkeyedRealisation>(const DrvOutput &)>;

/**
 * Just used in the implementation of `Store::queryPartialDerivationOutputMap`.
 */
void queryPartialDerivationOutputMapCA(
    Store & store,
    const StorePath & drvPath,
    const BasicDerivation & drv,
    std::map<std::string, std::optional<StorePath>> & outputs,
    QueryRealisationFun queryRealisation = {});

/**
 * Like Store::queryStaticPartialDerivationOutput, but resolves the
 * derivation first if needed.
 *
 * @param queryRealisation Optional callback for querying realisations.
 *        If not provided, uses `store.queryRealisation()`.
 */
std::optional<StorePath> deepQueryPartialDerivationOutput(
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
 *        If not provided, uses `store.queryRealisation()`.
 */
std::map<std::string, std::optional<StorePath>> deepQueryPartialDerivationOutputMap(
    Store & store, const StorePath & drvPath, Store * evalStore = nullptr, QueryRealisationFun queryRealisation = {});

/**
 * Like deepQueryPartialDerivationOutputMap, but throws if any output
 * path is not known.
 *
 * @param queryRealisation Optional callback for querying realisations.
 *        If not provided, uses `store.queryRealisation()`.
 */
OutputPathMap deepQueryDerivationOutputMap(
    Store & store, const StorePath & drvPath, Store * evalStore = nullptr, QueryRealisationFun queryRealisation = {});

} // namespace nix
