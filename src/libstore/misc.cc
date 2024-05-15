#include "derivations.hh"
#include "parsed-derivations.hh"
#include "globals.hh"
#include "store-api.hh"
#include "thread-pool.hh"
#include "realisation.hh"
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


const ContentAddress * getDerivationCA(const BasicDerivation & drv)
{
    auto out = drv.outputs.find("out");
    if (out == drv.outputs.end())
        return nullptr;
    if (auto dof = std::get_if<DerivationOutput::CAFixed>(&out->second.raw)) {
        return &dof->ca;
    }
    return nullptr;
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

    std::function<void(ref<SingleDerivedPath>, const DerivedPathMap<StringSet>::ChildNode &)> enqueueDerivedPaths;

    enqueueDerivedPaths = [&](ref<SingleDerivedPath> inputDrv, const DerivedPathMap<StringSet>::ChildNode & inputNode) {
        if (!inputNode.value.empty())
            pool.enqueue(std::bind(doPath, DerivedPath::Built { inputDrv, inputNode.value }));
        for (const auto & [outputName, childNode] : inputNode.childMap)
            enqueueDerivedPaths(
                make_ref<SingleDerivedPath>(SingleDerivedPath::Built { inputDrv, outputName }),
                childNode);
    };

    auto mustBuildDrv = [&](const StorePath & drvPath, const Derivation & drv) {
        {
            auto state(state_.lock());
            state->willBuild.insert(drvPath);
        }

        for (const auto & [inputDrv, inputNode] : drv.inputDrvs.map) {
            enqueueDerivedPaths(makeConstantStorePathRef(inputDrv), inputNode);
        }
    };

    auto checkOutput = [&](
        const StorePath & drvPath, ref<Derivation> drv, const StorePath & outPath, ref<Sync<DrvState>> drvState_)
    {
        if (drvState_->lock()->done) return;

        SubstitutablePathInfos infos;
        auto * cap = getDerivationCA(*drv);
        querySubstitutablePathInfos({
            {
                outPath,
                cap ? std::optional { *cap } : std::nullopt,
            },
        }, infos);

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
            auto drvPathP = std::get_if<DerivedPath::Opaque>(&*bfd.drvPath);
            if (!drvPathP) {
                // TODO make work in this case.
                warn("Ignoring dynamic derivation %s while querying missing paths; not yet implemented", bfd.drvPath->to_string(*this));
                return;
            }
            auto & drvPath = drvPathP->path;

            if (!isValidPath(drvPath)) {
                // FIXME: we could try to substitute the derivation.
                auto state(state_.lock());
                state->unknown.insert(drvPath);
                return;
            }

            StorePathSet invalid;
            /* true for regular derivations, and CA derivations for which we
               have a trust mapping for all wanted outputs. */
            auto knownOutputPaths = true;
            for (auto & [outputName, pathOpt] : queryPartialDerivationOutputMap(drvPath)) {
                if (!pathOpt) {
                    knownOutputPaths = false;
                    break;
                }
                if (bfd.outputs.contains(outputName) && !isValidPath(*pathOpt))
                    invalid.insert(*pathOpt);
            }
            if (knownOutputPaths && invalid.empty()) return;

            auto drv = make_ref<Derivation>(derivationFromPath(drvPath));
            ParsedDerivation parsedDrv(StorePath(drvPath), *drv);

            if (!knownOutputPaths && settings.useSubstitutes && parsedDrv.substitutesAllowed()) {
                experimentalFeatureSettings.require(Xp::CaDerivations);

                // If there are unknown output paths, attempt to find if the
                // paths are known to substituters through a realisation.
                auto outputHashes = staticOutputHashes(*this, *drv);
                knownOutputPaths = true;

                for (auto [outputName, hash] : outputHashes) {
                    if (!bfd.outputs.contains(outputName))
                        continue;

                    bool found = false;
                    for (auto &sub : getDefaultSubstituters()) {
                        auto realisation = sub->queryRealisation({hash, outputName});
                        if (!realisation)
                            continue;
                        found = true;
                        if (!isValidPath(realisation->outPath))
                            invalid.insert(realisation->outPath);
                        break;
                    }
                    if (!found) {
                        // Some paths did not have a realisation, this must be built.
                        knownOutputPaths = false;
                        break;
                    }
                }
            }

            if (knownOutputPaths && settings.useSubstitutes && parsedDrv.substitutesAllowed()) {
                auto drvState = make_ref<Sync<DrvState>>(DrvState(invalid.size()));
                for (auto & output : invalid)
                    pool.enqueue(std::bind(checkOutput, drvPath, drv, output, drvState));
            } else
                mustBuildDrv(drvPath, *drv);

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
    const StorePath & outputPath,
    Store * evalStore_)
{
    auto & evalStore = evalStore_ ? *evalStore_ : store;

    std::set<Realisation> inputRealisations;

    std::function<void(const StorePath &, const DerivedPathMap<StringSet>::ChildNode &)> accumRealisations;

    accumRealisations = [&](const StorePath & inputDrv, const DerivedPathMap<StringSet>::ChildNode & inputNode) {
        if (!inputNode.value.empty()) {
            auto outputHashes =
                staticOutputHashes(evalStore, evalStore.readDerivation(inputDrv));
            for (const auto & outputName : inputNode.value) {
                auto outputHash = get(outputHashes, outputName);
                if (!outputHash)
                    throw Error(
                        "output '%s' of derivation '%s' isn't realised", outputName,
                        store.printStorePath(inputDrv));
                auto thisRealisation = store.queryRealisation(
                    DrvOutput{*outputHash, outputName});
                if (!thisRealisation)
                    throw Error(
                        "output '%s' of derivation '%s' isn’t built", outputName,
                        store.printStorePath(inputDrv));
                inputRealisations.insert(*thisRealisation);
            }
        }
        if (!inputNode.value.empty()) {
            auto d = makeConstantStorePathRef(inputDrv);
            for (const auto & [outputName, childNode] : inputNode.childMap) {
                SingleDerivedPath next = SingleDerivedPath::Built { d, outputName };
                accumRealisations(
                    // TODO deep resolutions for dynamic derivations, issue #8947, would go here.
                    resolveDerivedPath(store, next, evalStore_),
                    childNode);
            }
        }
    };

    for (const auto & [inputDrv, inputNode] : drv.inputDrvs.map)
        accumRealisations(inputDrv, inputNode);

    auto info = store.queryPathInfo(outputPath);

    return drvOutputReferences(Realisation::closure(store, inputRealisations), info->references);
}

OutputPathMap resolveDerivedPath(Store & store, const DerivedPath::Built & bfd, Store * evalStore_)
{
    auto drvPath = resolveDerivedPath(store, *bfd.drvPath, evalStore_);

    auto outputsOpt_ = store.queryPartialDerivationOutputMap(drvPath, evalStore_);

    auto outputsOpt = std::visit(overloaded {
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
                        bfd.drvPath->to_string(store), output);
                outputsOpt.insert_or_assign(output, std::move(*pOutputPathOpt));
            }
            return outputsOpt;
        },
    }, bfd.outputs.raw);

    OutputPathMap outputs;
    for (auto & [outputName, outputPathOpt] : outputsOpt) {
        if (!outputPathOpt)
            throw MissingRealisation(bfd.drvPath->to_string(store), outputName);
        auto & outputPath = *outputPathOpt;
        outputs.insert_or_assign(outputName, outputPath);
    }
    return outputs;
}


StorePath resolveDerivedPath(Store & store, const SingleDerivedPath & req, Store * evalStore_)
{
    auto & evalStore = evalStore_ ? *evalStore_ : store;

    return std::visit(overloaded {
        [&](const SingleDerivedPath::Opaque & bo) {
            return bo.path;
        },
        [&](const SingleDerivedPath::Built & bfd) {
            auto drvPath = resolveDerivedPath(store, *bfd.drvPath, evalStore_);
            auto outputPaths = evalStore.queryPartialDerivationOutputMap(drvPath, evalStore_);
            if (outputPaths.count(bfd.output) == 0)
                throw Error("derivation '%s' does not have an output named '%s'",
                    store.printStorePath(drvPath), bfd.output);
            auto & optPath = outputPaths.at(bfd.output);
            if (!optPath)
                throw MissingRealisation(bfd.drvPath->to_string(store), bfd.output);
            return *optPath;
        },
    }, req.raw());
}


OutputPathMap resolveDerivedPath(Store & store, const DerivedPath::Built & bfd)
{
    auto drvPath = resolveDerivedPath(store, *bfd.drvPath);
    auto outputMap = store.queryDerivationOutputMap(drvPath);
    auto outputsLeft = std::visit(overloaded {
        [&](const OutputsSpec::All &) {
            return StringSet {};
        },
        [&](const OutputsSpec::Names & names) {
            return static_cast<StringSet>(names);
        },
    }, bfd.outputs.raw);
    for (auto iter = outputMap.begin(); iter != outputMap.end();) {
        auto & outputName = iter->first;
        if (bfd.outputs.contains(outputName)) {
            outputsLeft.erase(outputName);
            ++iter;
        } else {
            iter = outputMap.erase(iter);
        }
    }
    if (!outputsLeft.empty())
        throw Error("derivation '%s' does not have an outputs %s",
            store.printStorePath(drvPath),
            concatStringsSep(", ", quoteStrings(std::get<OutputsSpec::Names>(bfd.outputs.raw))));
    return outputMap;
}

}
