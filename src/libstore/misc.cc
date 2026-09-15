#include "nix/store/derivations.hh"
#include "nix/store/outputs-query.hh"
#include "nix/store/parsed-derivations.hh"
#include "nix/store/derivation/full-inputs.hh"
#include "nix/store/globals.hh"
#include "nix/store/store-open.hh"
#include "nix/store/nar-info.hh"
#include "nix/store/realisation.hh"
#include "nix/util/topo-sort.hh"
#include "nix/util/callback.hh"
#include "nix/util/closure.hh"
#include "nix/store/filetransfer.hh"
#include "nix/util/strings.hh"
#include "nix/util/json-utils.hh"

#include <boost/unordered/unordered_flat_set.hpp>

namespace nix {

void Store::computeFSClosure(
    const StorePathSet & startPaths,
    StorePathSet & paths_,
    bool flipDirection,
    bool includeOutputs,
    bool includeDerivers)
{
    std::function<asio::awaitable<StorePathSet>(const StorePath & path)> queryDeps;
    if (flipDirection)
        queryDeps = [this, includeOutputs, includeDerivers](const StorePath & path) -> asio::awaitable<StorePathSet> {
            StorePathSet res;
            StorePathSet referrers;
            queryReferrers(path, referrers);
            for (auto & ref : referrers)
                if (ref != path)
                    res.insert(ref);

            if (includeOutputs)
                for (auto & i : queryValidDerivers(path))
                    res.insert(i);

            if (includeDerivers && path.isDerivation())
                for (auto & [_, maybeOutPath] : queryPartialDerivationOutputMap(path))
                    if (maybeOutPath && isValidPath(*maybeOutPath))
                        res.insert(*maybeOutPath);
            co_return res;
        };
    else
        queryDeps = [this, includeOutputs, includeDerivers](const StorePath & path) -> asio::awaitable<StorePathSet> {
            StorePathSet res;
            auto info = co_await callbackToAwaitable<ref<const ValidPathInfo>>(
                [this, path](Callback<ref<const ValidPathInfo>> cb) { queryPathInfo(path, std::move(cb)); });

            for (auto & ref : info->references)
                if (ref != path)
                    res.insert(ref);

            if (includeOutputs && path.isDerivation())
                for (auto & [_, maybeOutPath] : queryPartialDerivationOutputMap(path))
                    if (maybeOutPath && isValidPath(*maybeOutPath))
                        res.insert(*maybeOutPath);

            if (includeDerivers && info->deriver && isValidPath(*info->deriver))
                res.insert(*info->deriver);
            co_return res;
        };

    computeClosure<StorePath>(startPaths, paths_, GetEdgesAsync<StorePath>(queryDeps));
}

void Store::computeFSClosure(
    const StorePath & startPath, StorePathSet & paths_, bool flipDirection, bool includeOutputs, bool includeDerivers)
{
    StorePathSet paths;
    paths.insert(startPath);
    computeFSClosure(paths, paths_, flipDirection, includeOutputs, includeDerivers);
}

const ContentAddress * getDerivationCA(const Derivation & drv)
{
    auto out = drv.outputs.find("out");
    if (out == drv.outputs.end())
        return nullptr;
    if (auto dof = std::get_if<DerivationOutput::CAFixed>(&out->second.raw)) {
        return &dof->ca;
    }
    return nullptr;
}

static asio::awaitable<void>
querySubstitutablePathInfosAsync(Store & store, const StorePathCAMap & paths, SubstitutablePathInfos & infos)
{
    if (!settings.getWorkerSettings().useSubstitutes)
        co_return;

    co_await forEachAsync(paths, [&store, &infos](auto path) -> asio::awaitable<void> {
        std::optional<Error> lastStoresException = std::nullopt;
        for (auto & sub : getDefaultSubstituters()) {
            if (lastStoresException.has_value()) {
                logError(lastStoresException->info());
                lastStoresException.reset();
            }

            auto subPath(path.first);

            // Recompute store path so that we can use a different store root.
            if (path.second) {
                subPath = store.makeFixedOutputPathFromCA(
                    path.first.name(), ContentAddressWithReferences::withoutRefs(*path.second));
                if (sub->storeDir == store.storeDir)
                    assert(subPath == path.first);
                if (subPath != path.first)
                    debug(
                        "replaced path '%s' with '%s' for substituter '%s'",
                        store.printStorePath(path.first),
                        sub->printStorePath(subPath),
                        sub->config.getHumanReadableURI());
            } else if (sub->storeDir != store.storeDir)
                continue;

            debug(
                "checking substituter '%s' for path '%s'",
                sub->config.getHumanReadableURI(),
                sub->printStorePath(subPath));
            try {
                auto info = co_await callbackToAwaitable<ref<const ValidPathInfo>>(
                    [subPath, &sub](Callback<ref<const ValidPathInfo>> cb) {
                        sub->queryPathInfo(subPath, std::move(cb));
                    });

                if (sub->storeDir != store.storeDir && !(info->isContentAddressed(*sub) && info->references.empty()))
                    continue;

                auto narInfo = std::dynamic_pointer_cast<const NarInfo>(std::shared_ptr<const ValidPathInfo>(info));
                infos.insert_or_assign(
                    path.first,
                    SubstitutablePathInfo{
                        .deriver = info->deriver,
                        .references = info->references,
                        .downloadSize = narInfo ? narInfo->fileSize : 0,
                        .narSize = info->narSize,
                    });

                break; /* We are done. */
            } catch (InvalidPath &) {
            } catch (SubstituterDisabled &) {
            } catch (Error & e) {
                lastStoresException = std::make_optional(std::move(e));
            }
        }
        if (lastStoresException.has_value()) {
            if (!settings.getWorkerSettings().tryFallback) {
                throw std::move(*lastStoresException);
            } else
                logError(lastStoresException->info());
        }
    });
}

void Store::querySubstitutablePathInfos(const StorePathCAMap & paths, SubstitutablePathInfos & infos)
{
    asio::io_context ctx;
    std::exception_ptr ex;
    asio::co_spawn(ctx, querySubstitutablePathInfosAsync(*this, paths, infos), [&](std::exception_ptr e) { ex = e; });
    ctx.run();
    if (ex)
        std::rethrow_exception(ex);
}

StorePaths Store::topoSortPaths(const StorePathSet & paths)
{
    auto result = topoSort(paths, [&](const StorePath & path) {
        try {
            return queryPathInfo(path)->references;
        } catch (InvalidPath &) {
            return StorePathSet();
        }
    });

    return std::visit(
        overloaded{
            [&](const Cycle<StorePath> & cycle) -> StorePaths {
                throw BuildError(
                    BuildResult::Failure::OutputRejected,
                    "cycle detected in the references of '%s' from '%s'",
                    printStorePath(cycle.path),
                    printStorePath(cycle.parent));
            },
            [](const auto & sorted) { return sorted; }},
        result);
}

OutputPathMap resolveDerivedPath(Store & store, const DerivedPath::Built & bfd, Store * evalStore_)
{
    auto drvPath = resolveDerivedPath(store, *bfd.drvPath, evalStore_);

    auto outputsOpt_ = deepQueryPartialDerivationOutputMap(store, drvPath, evalStore_);

    auto outputsOpt = std::visit(
        overloaded{
            [&](const OutputsSpec::All &) {
                // Keep all outputs
                return std::move(outputsOpt_);
            },
            [&](const OutputsSpec::Names & names) {
                // Get just those mentioned by name
                std::map<std::string, std::optional<StorePath>> outputsOpt;
                for (auto & output : names) {
                    auto * pOutputPathOpt = get(outputsOpt_, output);
                    if (!pOutputPathOpt)
                        throw Error(
                            "the derivation '%s' doesn't have an output named '%s'",
                            bfd.drvPath->to_string(store),
                            output);
                    outputsOpt.insert_or_assign(output, std::move(*pOutputPathOpt));
                }
                return outputsOpt;
            },
        },
        bfd.outputs.raw);

    OutputPathMap outputs;
    for (auto & [outputName, outputPathOpt] : outputsOpt) {
        if (!outputPathOpt)
            throw MissingRealisation(store, *bfd.drvPath, drvPath, outputName);
        auto & outputPath = *outputPathOpt;
        outputs.insert_or_assign(outputName, outputPath);
    }
    return outputs;
}

StorePath resolveDerivedPath(Store & store, const SingleDerivedPath & req, Store * evalStore_)
{
    return std::visit(
        overloaded{
            [&](const SingleDerivedPath::Opaque & bo) { return bo.path; },
            [&](const SingleDerivedPath::Built & bfd) {
                auto drvPath = resolveDerivedPath(store, *bfd.drvPath, evalStore_);
                auto outPath = deepQueryPartialDerivationOutput(store, drvPath, bfd.output, evalStore_);
                if (!outPath)
                    throw MissingRealisation(store, *bfd.drvPath, drvPath, bfd.output);
                return *outPath;
            },
        },
        req.raw());
}

} // namespace nix

namespace nlohmann {

nix::TrustedFlag adl_serializer<nix::TrustedFlag>::from_json(const json & json)
{
    using namespace nix;
    return getBoolean(json) ? TrustedFlag::Trusted : TrustedFlag::NotTrusted;
}

void adl_serializer<nix::TrustedFlag>::to_json(json & json, const nix::TrustedFlag & trustedFlag)
{
    json = static_cast<bool>(trustedFlag);
}

} // namespace nlohmann
