#include "derivations.hh"
#include "parsed-derivations.hh"
#include "globals.hh"
#include "local-store.hh"
#include "store-api.hh"
#include "thread-pool.hh"
#include "topo-sort.hh"
#include "callback.hh"

namespace nix {


void Store::computeFSClosure(const StorePathSet & startPaths,
    StorePathSet & paths_, bool flipDirection, bool includeOutputs, bool includeDerivers)
{
    struct State
    {
        size_t pending;
        StorePathSet & paths;
        std::exception_ptr exc;
    };

    Sync<State> state_(State{0, paths_, 0});

    std::function<void(const StorePath &)> enqueue;

    std::condition_variable done;

    enqueue = [&](const StorePath & path) -> void {
        {
            auto state(state_.lock());
            if (state->exc) return;
            if (!state->paths.insert(path).second) return;
            state->pending++;
        }

        queryPathInfo(path, {[&](std::future<ref<const ValidPathInfo>> fut) {
            // FIXME: calls to isValidPath() should be async

            try {
                auto info = fut.get();

                if (flipDirection) {

                    StorePathSet referrers;
                    queryReferrers(path, referrers);
                    for (auto & ref : referrers)
                        if (ref != path)
                            enqueue(ref);

                    if (includeOutputs)
                        for (auto & i : queryValidDerivers(path))
                            enqueue(i);

                    if (includeDerivers && path.isDerivation())
                        for (auto & i : queryDerivationOutputs(path))
                            if (isValidPath(i) && queryPathInfo(i)->deriver == path)
                                enqueue(i);

                } else {

                    for (auto & ref : info->references)
                        enqueue(ref);

                    if (includeOutputs && path.isDerivation())
                        for (auto & i : queryDerivationOutputs(path))
                            if (isValidPath(i)) enqueue(i);

                    if (includeDerivers && info->deriver && isValidPath(*info->deriver))
                        enqueue(*info->deriver);

                }

                {
                    auto state(state_.lock());
                    assert(state->pending);
                    if (!--state->pending) done.notify_one();
                }

            } catch (...) {
                auto state(state_.lock());
                if (!state->exc) state->exc = std::current_exception();
                assert(state->pending);
                if (!--state->pending) done.notify_one();
            };
        }});
    };

    for (auto & startPath : startPaths)
        enqueue(startPath);

    {
        auto state(state_.lock());
        while (state->pending) state.wait(done);
        if (state->exc) std::rethrow_exception(state->exc);
    }
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
    if (out == drv.outputs.end())
        return std::nullopt;
    if (auto dof = std::get_if<DerivationOutputCAFixed>(&out->second.output)) {
        return std::visit(overloaded {
            [&](TextInfo ti) -> std::optional<ContentAddress> {
                if (!ti.references.empty())
                    return std::nullopt;
                return static_cast<TextHash>(ti);
            },
            [&](FixedOutputInfo fi) -> std::optional<ContentAddress> {
                if (fi.references != PathReferences<StorePath> {})
                    return std::nullopt;
                return static_cast<FixedOutputHash>(fi);
            },
        }, dof->ca);
    }
    return std::nullopt;
}

void Store::queryMissing(const std::vector<DerivedPath> & targets,
    StorePathSet & willBuild_, StorePathSet & willSubstitute_, StorePathSet & unknown_,
    uint64_t & downloadSize_, uint64_t & narSize_)
{
    Activity act(*logger, lvlDebug, actUnknown, "querying info about missing paths");

    downloadSize_ = narSize_ = 0;

    ThreadPool pool;

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

    std::function<void(std::shared_ptr<SingleDerivedPath>, const DerivedPathMap<StringSet>::Node &)> accumDerivedPath;

    accumDerivedPath = [&](std::shared_ptr<SingleDerivedPath> inputDrv, const DerivedPathMap<StringSet>::Node & inputNode) {
        if (!inputNode.value.empty())
            pool.enqueue(std::bind(doPath, DerivedPath::Built { inputDrv, inputNode.value }));
        for (const auto & [outputName, childNode] : inputNode.childMap)
            accumDerivedPath(
                std::make_shared<SingleDerivedPath>(SingleDerivedPath::Built { inputDrv, outputName }),
                childNode);
    };

    auto mustBuildDrv = [&](const StorePath & drvPath, const Derivation & drv) {
        {
            auto state(state_.lock());
            state->willBuild.insert(drvPath);
        }

        for (const auto & [inputDrv, inputNode] : drv.inputDrvs.map) {
            accumDerivedPath(staticDrvReq(inputDrv), inputNode);
        }
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
          [&](DerivedPath::Built bfd) {
            auto drvPathP = std::get_if<DerivedPath::Opaque>(&*bfd.drvPath);
            if (!drvPathP) return; // TODO make work in this case.
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
                if (wantOutput(outputName, bfd.outputs) && !isValidPath(*pathOpt))
                    invalid.insert(*pathOpt);
            }
            if (knownOutputPaths && invalid.empty()) return;

            auto drv = make_ref<Derivation>(derivationFromPath(drvPath));
            ParsedDerivation parsedDrv(StorePath(drvPath), *drv);

            if (knownOutputPaths && settings.useSubstitutes && parsedDrv.substitutesAllowed()) {
                auto drvState = make_ref<Sync<DrvState>>(DrvState(invalid.size()));
                for (auto & output : invalid)
                    pool.enqueue(std::bind(checkOutput, drvPath, drv, output, drvState));
            } else
                mustBuildDrv(drvPath, *drv);

          },
          [&](DerivedPath::Opaque bo) {

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
            StorePathSet references;
            try {
                references = queryPathInfo(path)->references;
            } catch (InvalidPath &) {
            }
            return references;
        }},
        {[&](const StorePath & path, const StorePath & parent) {
            return BuildError(
                "cycle detected in the references of '%s' from '%s'",
                printStorePath(path),
                printStorePath(parent));
        }});
}


SingleDerivedPath tryResolveDerivedPath(Store & store, const SingleDerivedPath & req)
{
    return std::visit(overloaded {
        [&](const SingleDerivedPath::Opaque & _) -> SingleDerivedPath {
            return req;
        },
        [&](const SingleDerivedPath::Built & bfd0) -> SingleDerivedPath {
            SingleDerivedPath::Built bfd {
                std::make_shared<SingleDerivedPath>(tryResolveDerivedPath(store, *bfd0.drvPath)),
                bfd0.outputs,
            };
            return std::visit(overloaded {
                [&](const SingleDerivedPath::Opaque & bo) -> SingleDerivedPath {
                    auto & drvPath = bo.path;
                    auto outputPaths = store.queryPartialDerivationOutputMap(drvPath);
                    if (outputPaths.count(bfd.outputs) == 0)
                        throw Error("derivation '%s' does not have an output named '%s'",
                            store.printStorePath(drvPath), bfd.outputs);
                    auto & optPath = outputPaths.at(bfd.outputs);
                    if (optPath)
                        // Can resolve this step
                        return DerivedPath::Opaque { *optPath };
                    else
                        // Can't resolve this step
                        return bfd;
                },
                [&](const SingleDerivedPath::Built & _) -> SingleDerivedPath {
                    // Can't resolve previous step, and thus all future steps.
                    return bfd;
                },
            }, bfd.drvPath->raw());
        },
    }, req.raw());
}

#if 0
DerivedPath tryResolveDerivedPath(Store &, const DerivedPath &)
{
    // TODO
}
#endif


StorePath resolveDerivedPathWithHints(Store & store, const SingleDerivedPathWithHints & req)
{
    return std::visit(overloaded {
        [&](SingleDerivedPathWithHints::Opaque bo) {
            return bo.path;
        },
        [&](SingleDerivedPathWithHints::Built bfd) {
            if (bfd.outputs.second)
                return *bfd.outputs.second;
            else
                return resolveDerivedPath(store, SingleDerivedPath::Built {
                    staticDrvReq(resolveDerivedPathWithHints(store, *bfd.drvPath)),
                    bfd.outputs.first,
                });
        },
    }, req.raw());
}

StorePath resolveDerivedPath(Store & store, const SingleDerivedPath & req)
{
    return std::visit(overloaded {
        [&](SingleDerivedPath::Opaque bo) {
            return bo.path;
        },
        [&](SingleDerivedPath::Built bfd) {
            auto drvPath = resolveDerivedPath(store, *bfd.drvPath);
            auto outputPaths = store.queryPartialDerivationOutputMap(drvPath);
            if (outputPaths.count(bfd.outputs) == 0)
                throw Error("derivation '%s' does not have an output named '%s'",
                    store.printStorePath(drvPath), bfd.outputs);
            auto & optPath = outputPaths.at(bfd.outputs);
            if (!optPath)
                throw Error("'%s' does not yet map to a known concrete store path",
                    bfd.to_string(store));
            return *optPath;
        },
    }, req.raw());
}


std::map<std::string, StorePath> resolveDerivedPathWithHints(Store & store, const DerivedPathWithHints::Built & bfd)
{
    std::map<std::string, StorePath> res;
    for (auto & [outputName, optOutputPath] : bfd.outputs) {
        if (optOutputPath)
            res.insert_or_assign(outputName, *optOutputPath);
        else {
            // fallback on resolving anew
            StringSet outputNames;
            for (auto & [outputName, _] : bfd.outputs)
                outputNames.insert(outputName);
            return resolveDerivedPath(store, DerivedPath::Built {
                staticDrvReq(resolveDerivedPathWithHints(store, *bfd.drvPath)),
                outputNames,
            });
        }
    }
    return res;
}

std::map<std::string, StorePath> resolveDerivedPath(Store & store, const DerivedPath::Built & bfd)
{
    auto drvPath = resolveDerivedPath(store, *bfd.drvPath);
    auto outputMap = store.queryDerivationOutputMap(drvPath);
    auto outputsLefts = bfd.outputs;
    for (auto iter = outputMap.begin(); iter != outputMap.end();) {
        auto & outputName = iter->first;
        if (wantOutput(outputName, outputsLefts)) {
            outputsLefts.erase(outputName);
            ++iter;
        } else {
            iter = outputMap.erase(iter);
        }
    }
    if (!outputsLefts.empty())
        throw Error("derivation '%s' does not have an outputs %s",
            store.printStorePath(drvPath),
            concatStringsSep(", ", quoteStrings(bfd.outputs)));
    return outputMap;
}

}
