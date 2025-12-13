#include "nix/store/outputs-query.hh"
#include "nix/store/derivations.hh"
#include "nix/store/realisation.hh"

namespace nix {

/**
 * Resolve a derivation and compute its store path.
 *
 * @param queryRealisation must already be initialized (not empty)
 */
static std::pair<Derivation, StorePath>
resolveDerivation(Store & store, const StorePath & drvPath, Store * evalStore_, QueryRealisationFun & queryRealisation)
{
    auto & evalStore = evalStore_ ? *evalStore_ : store;

    Derivation drv = evalStore.readInvalidDerivation(drvPath);
    if (drv.shouldResolve()) {
        /* Without a custom queryRealisation, we could just use
           drv.tryResolve(store, &evalStore). But we need to use the
           callback variant to ensure all realisation queries go
           through queryRealisation. */
        auto resolvedDrv = drv.tryResolve(
            store,
            [&](ref<const SingleDerivedPath> depDrvPath,
                const std::string & depOutputName) -> std::optional<StorePath> {
                auto * opaque = std::get_if<SingleDerivedPath::Opaque>(&depDrvPath->raw());
                if (!opaque)
                    return std::nullopt;
                return deepQueryPartialDerivationOutput(
                           store, opaque->path, depOutputName, evalStore_, queryRealisation)
                    .outPath;
            });
        if (resolvedDrv)
            drv = Derivation{*resolvedDrv};
    }

    auto resolvedDrvPath = computeStorePath(store, drv);
    return {std::move(drv), std::move(resolvedDrvPath)};
}

void queryPartialDerivationOutputMapCA(
    Store & store,
    const StorePath & drvPath,
    const BasicDerivation & drv,
    std::map<std::string, std::optional<StorePath>> & outputs,
    QueryRealisationFun queryRealisation)
{
    if (!queryRealisation)
        queryRealisation = [&store](const DrvOutput & o) { return store.queryRealisation(o); };

    for (auto & [outputName, _] : drv.outputs) {
        auto realisation = queryRealisation(DrvOutput{drvPath, outputName});
        if (realisation) {
            outputs.insert_or_assign(outputName, realisation->outPath);
        } else {
            outputs.insert({outputName, std::nullopt});
        }
    }
}

std::map<std::string, std::optional<StorePath>> deepQueryPartialDerivationOutputMap(
    Store & store, const StorePath & drvPath, Store * evalStore_, QueryRealisationFun queryRealisation)
{
    auto & evalStore = evalStore_ ? *evalStore_ : store;

    if (!queryRealisation)
        queryRealisation = [&store](const DrvOutput & o) { return store.queryRealisation(o); };

    auto outputs = evalStore.queryStaticPartialDerivationOutputMap(drvPath);

    if (!experimentalFeatureSettings.isEnabled(Xp::CaDerivations))
        return outputs;

    auto [drv, resolvedDrvPath] = resolveDerivation(store, drvPath, evalStore_, queryRealisation);
    queryPartialDerivationOutputMapCA(store, resolvedDrvPath, drv, outputs, queryRealisation);

    return outputs;
}

OutputPathMap deepQueryDerivationOutputMap(
    Store & store, const StorePath & drvPath, Store * evalStore, QueryRealisationFun queryRealisation)
{
    auto resp = deepQueryPartialDerivationOutputMap(store, drvPath, evalStore, std::move(queryRealisation));
    OutputPathMap result;
    for (auto & [outName, optOutPath] : resp) {
        if (!optOutPath)
            throw MissingRealisation(store, drvPath, outName);
        result.insert_or_assign(outName, *optOutPath);
    }
    return result;
}

DeepDerivationOutputResult deepQueryPartialDerivationOutput(
    Store & store,
    const StorePath & drvPath,
    const std::string & outputName,
    Store * evalStore_,
    QueryRealisationFun queryRealisation)
{
    auto & evalStore = evalStore_ ? *evalStore_ : store;

    if (!queryRealisation)
        queryRealisation = [&store](const DrvOutput & o) { return store.queryRealisation(o); };

    auto staticResult = evalStore.queryStaticPartialDerivationOutput(drvPath, outputName);
    if (staticResult || !experimentalFeatureSettings.isEnabled(Xp::CaDerivations))
        return {
            .outPath = staticResult,
            .resolvedDrvPath = drvPath,
        };

    auto [drv, resolvedDrvPath] = resolveDerivation(store, drvPath, evalStore_, queryRealisation);

    if (drv.outputs.count(outputName) == 0)
        throw Error("derivation '%s' does not have an output named '%s'", store.printStorePath(drvPath), outputName);

    auto realisation = queryRealisation(DrvOutput{resolvedDrvPath, outputName});
    return {
        .outPath = realisation ? std::optional{realisation->outPath} : std::nullopt,
        .resolvedDrvPath = resolvedDrvPath,
    };
}

} // namespace nix
