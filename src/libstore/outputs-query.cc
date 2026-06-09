#include "nix/store/outputs-query.hh"
#include "nix/store/derivations.hh"
#include "nix/store/realisation.hh"
#include "nix/util/util.hh"

#include <boost/unordered/unordered_flat_map.hpp>

namespace nix {

namespace {

/**
 * Cache mapping an unresolved drv path to its resolved (Derivation, StorePath)
 * pair. Shared across a single top-level resolution call to prevent exponential
 * re-traversal of the closure when many derivations share the same dependencies.
 */
using ResolveCache = boost::unordered_flat_map<StorePath, std::pair<Derivation, StorePath>>;

/**
 * Cache mapping a resolved derivation output to its realisation output path.
 */
using RealisationCache = boost::unordered_flat_map<DrvOutput, std::optional<StorePath>>;

/* Forward declaration so resolveSingleDerivedPath can call it. */
static std::optional<StorePath> deepQueryPartialDerivationOutputImpl(
    Store & store,
    const StorePath & drvPath,
    const std::string & outputName,
    Store * evalStore_,
    QueryRealisationFun & queryRealisation,
    ResolveCache & cache,
    RealisationCache & resCache);

/**
 * Resolve a `SingleDerivedPath` to a concrete store path.
 *
 * For `Opaque` paths this is trivial. For `Built` paths (dynamic
 * derivations), recursively resolve the inner path and query its
 * output.
 *
 * @param queryRealisation must already be initialized (not empty)
 */
static std::optional<StorePath> resolveSingleDerivedPath(
    Store & store,
    const SingleDerivedPath & path,
    Store * evalStore_,
    QueryRealisationFun & queryRealisation,
    ResolveCache & cache,
    RealisationCache & resCache)
{
    return std::visit(
        overloaded{
            [](const SingleDerivedPath::Opaque & opaque) -> std::optional<StorePath> { return opaque.path; },
            [&](const SingleDerivedPath::Built & built) -> std::optional<StorePath> {
                auto innerPath =
                    resolveSingleDerivedPath(store, *built.drvPath, evalStore_, queryRealisation, cache, resCache);
                if (!innerPath)
                    return std::nullopt;
                return deepQueryPartialDerivationOutputImpl(
                    store, *innerPath, built.output, evalStore_, queryRealisation, cache, resCache);
            },
        },
        path.raw());
}

/**
 * Resolve a derivation and compute its store path, with memoization.
 *
 * Results are stored in `cache` (keyed on the unresolved `drvPath`) so that
 * each derivation in the closure is resolved at most once per top-level call,
 * preventing the exponential re-traversal that would otherwise occur for
 * content-addressed derivation closures.
 *
 * @param queryRealisation must already be initialized (not empty)
 */
static std::pair<Derivation, StorePath> resolveDerivation(
    Store & store,
    const StorePath & drvPath,
    Store * evalStore_,
    QueryRealisationFun & queryRealisation,
    ResolveCache & cache,
    RealisationCache & resCache)
{
    auto it = cache.find(drvPath);
    if (it != cache.end())
        return it->second;

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
                auto concreteDrvPath =
                    resolveSingleDerivedPath(store, *depDrvPath, evalStore_, queryRealisation, cache, resCache);
                if (!concreteDrvPath)
                    return std::nullopt;
                return deepQueryPartialDerivationOutputImpl(
                    store, *concreteDrvPath, depOutputName, evalStore_, queryRealisation, cache, resCache);
            });
        if (resolvedDrv)
            drv = Derivation{*resolvedDrv};
    }

    auto resolvedDrvPath = computeStorePath(store, drv);
    auto result = std::make_pair(drv, resolvedDrvPath);
    cache.emplace(drvPath, result);
    return result;
}

void queryPartialDerivationOutputMapCA(
    Store & store,
    const StorePath & drvPath,
    const BasicDerivation & drv,
    std::map<std::string, std::optional<StorePath>> & outputs,
    QueryRealisationFun queryRealisation,
    RealisationCache & resCache)
{
    if (!queryRealisation)
        queryRealisation = [&store](const DrvOutput & o) { return store.queryRealisation(o); };

    for (auto & [outputName, _] : drv.outputs) {
        DrvOutput id{drvPath, outputName};
        auto it = resCache.find(id);
        if (it != resCache.end()) {
            outputs.insert_or_assign(outputName, it->second);
            continue;
        }

        auto realisation = queryRealisation(id);
        std::optional<StorePath> outPath = realisation ? std::optional{realisation->outPath} : std::nullopt;
        resCache.emplace(id, outPath);

        if (outPath) {
            outputs.insert_or_assign(outputName, *outPath);
        } else {
            outputs.insert({outputName, std::nullopt});
        }
    }
}

/**
 * Internal implementation of deepQueryPartialDerivationOutput that accepts a
 * shared ResolveCache and RealisationCache, allowing memoization across recursive calls.
 */
static std::optional<StorePath> deepQueryPartialDerivationOutputImpl(
    Store & store,
    const StorePath & drvPath,
    const std::string & outputName,
    Store * evalStore_,
    QueryRealisationFun & queryRealisation,
    ResolveCache & cache,
    RealisationCache & resCache)
{
    auto & evalStore = evalStore_ ? *evalStore_ : store;

    auto staticResult = evalStore.queryStaticPartialDerivationOutput(drvPath, outputName);
    if (staticResult || !experimentalFeatureSettings.isEnabled(Xp::CaDerivations))
        return staticResult;

    auto [drv, resolvedDrvPath] = resolveDerivation(store, drvPath, evalStore_, queryRealisation, cache, resCache);

    if (drv.outputs.count(outputName) == 0)
        throw Error("derivation '%s' does not have an output named '%s'", store.printStorePath(drvPath), outputName);

    DrvOutput id{resolvedDrvPath, outputName};
    auto it = resCache.find(id);
    if (it != resCache.end())
        return it->second;

    auto realisation = queryRealisation(id);
    std::optional<StorePath> outPath = realisation ? std::optional{realisation->outPath} : std::nullopt;
    resCache.emplace(id, outPath);
    return outPath;
}

} // namespace

void queryPartialDerivationOutputMapCA(
    Store & store,
    const StorePath & drvPath,
    const BasicDerivation & drv,
    std::map<std::string, std::optional<StorePath>> & outputs,
    QueryRealisationFun queryRealisation)
{
    RealisationCache resCache;
    queryPartialDerivationOutputMapCA(store, drvPath, drv, outputs, queryRealisation, resCache);
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

    ResolveCache cache;
    RealisationCache resCache;
    auto [drv, resolvedDrvPath] = resolveDerivation(store, drvPath, evalStore_, queryRealisation, cache, resCache);
    queryPartialDerivationOutputMapCA(store, resolvedDrvPath, drv, outputs, queryRealisation, resCache);

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

std::optional<StorePath> deepQueryPartialDerivationOutput(
    Store & store,
    const StorePath & drvPath,
    const std::string & outputName,
    Store * evalStore_,
    QueryRealisationFun queryRealisation)
{
    if (!queryRealisation)
        queryRealisation = [&store](const DrvOutput & o) { return store.queryRealisation(o); };

    ResolveCache cache;
    RealisationCache resCache;
    return deepQueryPartialDerivationOutputImpl(
        store, drvPath, outputName, evalStore_, queryRealisation, cache, resCache);
}

} // namespace nix
