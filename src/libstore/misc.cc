#include "derivations.hh"
#include "parsed-derivations.hh"
#include "globals.hh"
#include "local-store.hh"
#include "store-api.hh"
#include "thread-pool.hh"


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

    std::function<void(const Path &)> enqueue;

    std::condition_variable done;

    enqueue = [&](const Path & path) -> void {
        {
            auto state(state_.lock());
            if (state->exc) return;
            if (!state->paths.insert(parseStorePath(path)).second) return;
            state->pending++;
        }

        queryPathInfo(parseStorePath(path), {[&, pathS(path)](std::future<ref<const ValidPathInfo>> fut) {
            // FIXME: calls to isValidPath() should be async

            try {
                auto info = fut.get();

                auto path = parseStorePath(pathS);

                if (flipDirection) {

                    StorePathSet referrers;
                    queryReferrers(path, referrers);
                    for (auto & ref : referrers)
                        if (ref != path)
                            enqueue(printStorePath(ref));

                    if (includeOutputs)
                        for (auto & i : queryValidDerivers(path))
                            enqueue(printStorePath(i));

                    if (includeDerivers && path.isDerivation())
                        for (auto & i : queryDerivationOutputs(path))
                            if (isValidPath(i) && queryPathInfo(i)->deriver == path)
                                enqueue(printStorePath(i));

                } else {

                    for (auto & ref : info->references)
                        if (ref != path)
                            enqueue(printStorePath(ref));

                    if (includeOutputs && path.isDerivation())
                        for (auto & i : queryDerivationOutputs(path))
                            if (isValidPath(i)) enqueue(printStorePath(i));

                    if (includeDerivers && info->deriver && isValidPath(*info->deriver))
                        enqueue(printStorePath(*info->deriver));

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
        enqueue(printStorePath(startPath));

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
    paths.insert(startPath.clone());
    computeFSClosure(paths, paths_, flipDirection, includeOutputs, includeDerivers);
}


void Store::queryMissing(const std::vector<StorePathWithOutputs> & targets,
    StorePathSet & willBuild_, StorePathSet & willSubstitute_, StorePathSet & unknown_,
    unsigned long long & downloadSize_, unsigned long long & narSize_)
{
    Activity act(*logger, lvlDebug, actUnknown, "querying info about missing paths");

    downloadSize_ = narSize_ = 0;

    ThreadPool pool;

    struct State
    {
        std::unordered_set<std::string> done;
        StorePathSet & unknown, & willSubstitute, & willBuild;
        unsigned long long & downloadSize;
        unsigned long long & narSize;
    };

    struct DrvState
    {
        size_t left;
        bool done = false;
        StorePathSet outPaths;
        DrvState(size_t left) : left(left) { }
    };

    Sync<State> state_(State{{}, unknown_, willSubstitute_, willBuild_, downloadSize_, narSize_});

    std::function<void(StorePathWithOutputs)> doPath;

    auto mustBuildDrv = [&](const StorePath & drvPath, const Derivation & drv) {
        {
            auto state(state_.lock());
            state->willBuild.insert(drvPath.clone());
        }

        for (auto & i : drv.inputDrvs)
            pool.enqueue(std::bind(doPath, StorePathWithOutputs(i.first, i.second)));
    };

    auto checkOutput = [&](
        const Path & drvPathS, ref<Derivation> drv, const Path & outPathS, ref<Sync<DrvState>> drvState_)
    {
        if (drvState_->lock()->done) return;

        auto drvPath = parseStorePath(drvPathS);
        auto outPath = parseStorePath(outPathS);

        SubstitutablePathInfos infos;
        StorePathSet paths; // FIXME
        paths.insert(outPath.clone());
        querySubstitutablePathInfos(paths, infos);

        if (infos.empty()) {
            drvState_->lock()->done = true;
            mustBuildDrv(drvPath, *drv);
        } else {
            {
                auto drvState(drvState_->lock());
                if (drvState->done) return;
                assert(drvState->left);
                drvState->left--;
                drvState->outPaths.insert(outPath.clone());
                if (!drvState->left) {
                    for (auto & path : drvState->outPaths)
                        pool.enqueue(std::bind(doPath, StorePathWithOutputs(path.clone())));
                }
            }
        }
    };

    doPath = [&](const StorePathWithOutputs & path) {

        {
            auto state(state_.lock());
            if (!state->done.insert(path.to_string(*this)).second) return;
        }

        if (path.path.isDerivation()) {
            if (!isValidPath(path.path)) {
                // FIXME: we could try to substitute the derivation.
                auto state(state_.lock());
                state->unknown.insert(path.path.clone());
                return;
            }

            auto drv = make_ref<Derivation>(derivationFromPath(path.path));
            ParsedDerivation parsedDrv(path.path.clone(), *drv);

            PathSet invalid;
            for (auto & j : drv->outputs)
                if (wantOutput(j.first, path.outputs)
                    && !isValidPath(j.second.path))
                    invalid.insert(printStorePath(j.second.path));
            if (invalid.empty()) return;

            if (settings.useSubstitutes && parsedDrv.substitutesAllowed()) {
                auto drvState = make_ref<Sync<DrvState>>(DrvState(invalid.size()));
                for (auto & output : invalid)
                    pool.enqueue(std::bind(checkOutput, printStorePath(path.path), drv, output, drvState));
            } else
                mustBuildDrv(path.path, *drv);

        } else {

            if (isValidPath(path.path)) return;

            SubstitutablePathInfos infos;
            StorePathSet paths; // FIXME
            paths.insert(path.path.clone());
            querySubstitutablePathInfos(paths, infos);

            if (infos.empty()) {
                auto state(state_.lock());
                state->unknown.insert(path.path.clone());
                return;
            }

            auto info = infos.find(path.path);
            assert(info != infos.end());

            {
                auto state(state_.lock());
                state->willSubstitute.insert(path.path.clone());
                state->downloadSize += info->second.downloadSize;
                state->narSize += info->second.narSize;
            }

            for (auto & ref : info->second.references)
                pool.enqueue(std::bind(doPath, StorePathWithOutputs(ref)));
        }
    };

    for (auto & path : targets)
        pool.enqueue(std::bind(doPath, path));

    pool.process();
}


StorePaths Store::topoSortPaths(const StorePathSet & paths)
{
    StorePaths sorted;
    StorePathSet visited, parents;

    std::function<void(const StorePath & path, const StorePath * parent)> dfsVisit;

    dfsVisit = [&](const StorePath & path, const StorePath * parent) {
        if (parents.count(path))
            throw BuildError("cycle detected in the references of '%s' from '%s'",
                printStorePath(path), printStorePath(*parent));

        if (!visited.insert(path.clone()).second) return;
        parents.insert(path.clone());

        StorePathSet references;
        try {
            references = cloneStorePathSet(queryPathInfo(path)->references);
        } catch (InvalidPath &) {
        }

        for (auto & i : references)
            /* Don't traverse into paths that don't exist.  That can
               happen due to substitutes for non-existent paths. */
            if (i != path && paths.count(i))
                dfsVisit(i, &path);

        sorted.push_back(path.clone());
        parents.erase(path);
    };

    for (auto & i : paths)
        dfsVisit(i, nullptr);

    std::reverse(sorted.begin(), sorted.end());

    return sorted;
}


}
