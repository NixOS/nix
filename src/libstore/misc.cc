#include "derivations.hh"
#include "parsed-derivations.hh"
#include "globals.hh"
#include "local-store.hh"
#include "store-api.hh"
#include "thread-pool.hh"
#include "topo-sort.hh"
#include "callback.hh"
#include "closure.hh"
#include "filetransfer.hh"

namespace nix {

void Store::computeFSClosure(const StorePathSet & startPaths,
    StorePathSet & paths_, bool flipDirection, bool includeOutputs, bool includeDerivers)
{
    std::function<std::set<StorePath>(const StorePath & path, std::future<ref<const ValidPathInfo>> &)> queryDeps;
    if (flipDirection)
        queryDeps = [&](const StorePath& path,
                        std::future<ref<const ValidPathInfo>> & fut) {
            StorePathSet res;
            StorePathSet referrers;
            queryReferrers(path, referrers);
            for (auto& ref : referrers)
                if (ref != path)
                    res.insert(ref);

            if (includeOutputs)
                for (auto& i : queryValidDerivers(path))
                    res.insert(i);

            if (includeDerivers && path.isDerivation())
                for (auto& [_, maybeOutPath] : queryPartialDerivationOutputMap(path))
                    if (maybeOutPath && isValidPath(*maybeOutPath))
                        res.insert(*maybeOutPath);
            return res;
        };
    else
        queryDeps = [&](const StorePath& path,
                        std::future<ref<const ValidPathInfo>> & fut) {
            StorePathSet res;
            auto info = fut.get();
            for (auto& ref : info->references)
                if (ref != path)
                    res.insert(ref);

            if (includeOutputs && path.isDerivation())
                for (auto& [_, maybeOutPath] : queryPartialDerivationOutputMap(path))
                    if (maybeOutPath && isValidPath(*maybeOutPath))
                        res.insert(*maybeOutPath);

            if (includeDerivers && info->deriver && isValidPath(*info->deriver))
                res.insert(*info->deriver);
            return res;
        };

    computeClosure<StorePath>(
        startPaths, paths_,
        [&](const StorePath& path,
            std::function<void(std::promise<std::set<StorePath>>&)>
                processEdges) {
            std::promise<std::set<StorePath>> promise;
            std::function<void(std::future<ref<const ValidPathInfo>>)>
                getDependencies =
                    [&](std::future<ref<const ValidPathInfo>> fut) {
                        try {
                            promise.set_value(queryDeps(path, fut));
                        } catch (...) {
                            promise.set_exception(std::current_exception());
                        }
                    };
            queryPathInfo(path, getDependencies);
            processEdges(promise);
        });
}

void Store::computeFSClosure(const StorePath & startPath,
    StorePathSet & paths_, bool flipDirection, bool includeOutputs, bool includeDerivers)
{
    StorePathSet paths;
    paths.insert(startPath);
    computeFSClosure(paths, paths_, flipDirection, includeOutputs, includeDerivers);
}


std::optional<ContentAddress> getDerivationCA(const BasicDerivation & drv)
{
    auto out = drv.outputs.find("out");
    if (out != drv.outputs.end()) {
        if (const auto * v = std::get_if<DerivationOutput::CAFixed>(&out->second.raw()))
            return v->hash;
    }
    return std::nullopt;
}

void Store::queryMissing(const std::vector<DerivedPath> & targets,
    StorePathSet & willBuild_, StorePathSet & willSubstitute_, StorePathSet & unknown_,
    uint64_t & downloadSize_, uint64_t & narSize_)
{
    Activity act(*logger, lvlDebug, actUnknown, "querying info about missing paths");

    downloadSize_ = narSize_ = 0;

    // FIXME: make async.
    ThreadPool pool(fileTransferSettings.httpConnections);

    struct State
    {
        std::unordered_set<std::string> done;
        StorePathSet & unknown, & willSubstitute, & willBuild;
        uint64_t & downloadSize;
        uint64_t & narSize;
    };

    struct DrvState
    {
        size_t left;
        bool done = false;
        StorePathSet outPaths;
        DrvState(size_t left) : left(left) { }
    };

    Sync<State> state_(State{{}, unknown_, willSubstitute_, willBuild_, downloadSize_, narSize_});

    std::function<void(DerivedPath)> doPath;

    auto mustBuildDrv = [&](const StorePath & drvPath, const Derivation & drv) {
        {
            auto state(state_.lock());
            state->willBuild.insert(drvPath);
        }

        for (auto & i : drv.inputDrvs)
            pool.enqueue(std::bind(doPath, DerivedPath::Built { i.first, i.second }));
    };

    auto checkOutput = [&](
        const StorePath & drvPath, ref<Derivation> drv, const StorePath & outPath, ref<Sync<DrvState>> drvState_)
    {
        if (drvState_->lock()->done) return;

        SubstitutablePathInfos infos;
        querySubstitutablePathInfos({{outPath, getDerivationCA(*drv)}}, infos);

        if (infos.empty()) {
            drvState_->lock()->done = true;
            mustBuildDrv(drvPath, *drv);
        } else {
            {
                auto drvState(drvState_->lock());
                if (drvState->done) return;
                assert(drvState->left);
                drvState->left--;
                drvState->outPaths.insert(outPath);
                if (!drvState->left) {
                    for (auto & path : drvState->outPaths)
                        pool.enqueue(std::bind(doPath, DerivedPath::Opaque { path } ));
                }
            }
        }
    };

    doPath = [&](const DerivedPath & req) {

        {
            auto state(state_.lock());
            if (!state->done.insert(req.to_string(*this)).second) return;
        }

        std::visit(overloaded {
          [&](const DerivedPath::Built & bfd) {
            if (!isValidPath(bfd.drvPath)) {
                // FIXME: we could try to substitute the derivation.
                auto state(state_.lock());
                state->unknown.insert(bfd.drvPath);
                return;
            }

            StorePathSet invalid;
            /* true for regular derivations, and CA derivations for which we
               have a trust mapping for all wanted outputs. */
            auto knownOutputPaths = true;
            for (auto & [outputName, pathOpt] : queryPartialDerivationOutputMap(bfd.drvPath)) {
                if (!pathOpt) {
                    knownOutputPaths = false;
                    break;
                }
                if (wantOutput(outputName, bfd.outputs) && !isValidPath(*pathOpt))
                    invalid.insert(*pathOpt);
            }
            if (knownOutputPaths && invalid.empty()) return;

            auto drv = make_ref<Derivation>(derivationFromPath(bfd.drvPath));
            ParsedDerivation parsedDrv(StorePath(bfd.drvPath), *drv);

            if (knownOutputPaths && settings.useSubstitutes && parsedDrv.substitutesAllowed()) {
                auto drvState = make_ref<Sync<DrvState>>(DrvState(invalid.size()));
                for (auto & output : invalid)
                    pool.enqueue(std::bind(checkOutput, bfd.drvPath, drv, output, drvState));
            } else
                mustBuildDrv(bfd.drvPath, *drv);

          },
          [&](const DerivedPath::Opaque & bo) {

            if (isValidPath(bo.path)) return;

            SubstitutablePathInfos infos;
            querySubstitutablePathInfos({{bo.path, std::nullopt}}, infos);

            if (infos.empty()) {
                auto state(state_.lock());
                state->unknown.insert(bo.path);
                return;
            }

            auto info = infos.find(bo.path);
            assert(info != infos.end());

            {
                auto state(state_.lock());
                state->willSubstitute.insert(bo.path);
                state->downloadSize += info->second.downloadSize;
                state->narSize += info->second.narSize;
            }

            for (auto & ref : info->second.references)
                pool.enqueue(std::bind(doPath, DerivedPath::Opaque { ref }));
          },
        }, req.raw());
    };

    for (auto & path : targets)
        pool.enqueue(std::bind(doPath, path));

    pool.process();
}


StorePaths Store::topoSortPaths(const StorePathSet & paths)
{
    return topoSort(paths,
        {[&](const StorePath & path) {
            try {
                return queryPathInfo(path)->references;
            } catch (InvalidPath &) {
                return StorePathSet();
            }
        }},
        {[&](const StorePath & path, const StorePath & parent) {
            return BuildError(
                "cycle detected in the references of '%s' from '%s'",
                printStorePath(path),
                printStorePath(parent));
        }});
}

std::map<DrvOutput, StorePath> drvOutputReferences(
    const std::set<Realisation> & inputRealisations,
    const StorePathSet & pathReferences)
{
    std::map<DrvOutput, StorePath> res;

    for (const auto & input : inputRealisations) {
        if (pathReferences.count(input.outPath)) {
            res.insert({input.id, input.outPath});
        }
    }

    return res;
}

std::map<DrvOutput, StorePath> drvOutputReferences(
    Store & store,
    const Derivation & drv,
    const StorePath & outputPath)
{
    std::set<Realisation> inputRealisations;

    for (const auto & [inputDrv, outputNames] : drv.inputDrvs) {
        auto outputHashes =
            staticOutputHashes(store, store.readDerivation(inputDrv));
        for (const auto & outputName : outputNames) {
            auto thisRealisation = store.queryRealisation(
                DrvOutput{outputHashes.at(outputName), outputName});
            if (!thisRealisation)
                throw Error(
                    "output '%s' of derivation '%s' isn't built", outputName,
                    store.printStorePath(inputDrv));
            inputRealisations.insert(*thisRealisation);
        }
    }

    auto info = store.queryPathInfo(outputPath);

    return drvOutputReferences(Realisation::closure(store, inputRealisations), info->references);
}

}
