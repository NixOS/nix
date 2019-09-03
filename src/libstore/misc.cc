#include "derivations.hh"
#include "parsed-derivations.hh"
#include "globals.hh"
#include "local-store.hh"
#include "store-api.hh"
#include "thread-pool.hh"


namespace nix {


void Store::computeFSClosure(const PathSet & startPaths,
    PathSet & paths_, bool flipDirection, bool includeOutputs, bool includeDerivers)
{
    struct State
    {
        size_t pending;
        PathSet & paths;
        std::exception_ptr exc;
    };

    Sync<State> state_(State{0, paths_, 0});

    std::function<void(const Path &)> enqueue;

    std::condition_variable done;

    enqueue = [&](const Path & path) -> void {
        {
            auto state(state_.lock());
            if (state->exc) return;
            if (state->paths.count(path)) return;
            state->paths.insert(path);
            state->pending++;
        }

        queryPathInfo(path, {[&, path](std::future<ref<ValidPathInfo>> fut) {
            // FIXME: calls to isValidPath() should be async

            try {
                auto info = fut.get();

                if (flipDirection) {

                    PathSet referrers;
                    queryReferrers(path, referrers);
                    for (auto & ref : referrers)
                        if (ref != path)
                            enqueue(ref);

                    if (includeOutputs)
                        for (auto & i : queryValidDerivers(path))
                            enqueue(i);

                    if (includeDerivers && isDerivation(path))
                        for (auto & i : queryDerivationOutputs(path))
                            if (isValidPath(i) && queryPathInfo(i)->deriver == path)
                                enqueue(i);

                } else {

                    for (auto & ref : info->references)
                        if (ref != path)
                            enqueue(ref);

                    if (includeOutputs && isDerivation(path))
                        for (auto & i : queryDerivationOutputs(path))
                            if (isValidPath(i)) enqueue(i);

                    if (includeDerivers && isValidPath(info->deriver))
                        enqueue(info->deriver);

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


void Store::computeFSClosure(const Path & startPath,
    PathSet & paths_, bool flipDirection, bool includeOutputs, bool includeDerivers)
{
    computeFSClosure(PathSet{startPath}, paths_, flipDirection, includeOutputs, includeDerivers);
}


void Store::queryMissing(const PathSet & targets,
    PathSet & willBuild_, PathSet & willSubstitute_, PathSet & unknown_,
    unsigned long long & downloadSize_, unsigned long long & narSize_)
{
    Activity act(*logger, lvlDebug, actUnknown, "querying info about missing paths");

    downloadSize_ = narSize_ = 0;

    ThreadPool pool;

    struct State
    {
        PathSet done;
        PathSet & unknown, & willSubstitute, & willBuild;
        unsigned long long & downloadSize;
        unsigned long long & narSize;
    };

    struct DrvState
    {
        size_t left;
        bool done = false;
        PathSet outPaths;
        DrvState(size_t left) : left(left) { }
    };

    Sync<State> state_(State{PathSet(), unknown_, willSubstitute_, willBuild_, downloadSize_, narSize_});

    std::function<void(Path)> doPath;

    auto mustBuildDrv = [&](const Path & drvPath, const Derivation & drv) {
        {
            auto state(state_.lock());
            state->willBuild.insert(drvPath);
        }

        for (auto & i : drv.inputDrvs)
            pool.enqueue(std::bind(doPath, makeDrvPathWithOutputs(i.first, i.second)));
    };

    auto checkOutput = [&](
        const Path & drvPath, ref<Derivation> drv, const Path & outPath, ref<Sync<DrvState>> drvState_)
    {
        if (drvState_->lock()->done) return;

        SubstitutablePathInfos infos;
        querySubstitutablePathInfos({outPath}, infos);

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
                        pool.enqueue(std::bind(doPath, path));
                }
            }
        }
    };

    doPath = [&](const Path & path) {

        {
            auto state(state_.lock());
            if (state->done.count(path)) return;
            state->done.insert(path);
        }

        DrvPathWithOutputs i2 = parseDrvPathWithOutputs(path);

        if (isDerivation(i2.first)) {
            if (!isValidPath(i2.first)) {
                // FIXME: we could try to substitute the derivation.
                auto state(state_.lock());
                state->unknown.insert(path);
                return;
            }

            Derivation drv = derivationFromPath(i2.first);
            ParsedDerivation parsedDrv(i2.first, drv);

            PathSet invalid;
            for (auto & j : drv.outputs)
                if (wantOutput(j.first, i2.second)
                    && !isValidPath(j.second.path))
                    invalid.insert(j.second.path);
            if (invalid.empty()) return;

            if (settings.useSubstitutes && parsedDrv.substitutesAllowed()) {
                auto drvState = make_ref<Sync<DrvState>>(DrvState(invalid.size()));
                for (auto & output : invalid)
                    pool.enqueue(std::bind(checkOutput, i2.first, make_ref<Derivation>(drv), output, drvState));
            } else
                mustBuildDrv(i2.first, drv);

        } else {

            if (isValidPath(path)) return;

            SubstitutablePathInfos infos;
            querySubstitutablePathInfos({path}, infos);

            if (infos.empty()) {
                auto state(state_.lock());
                state->unknown.insert(path);
                return;
            }

            auto info = infos.find(path);
            assert(info != infos.end());

            {
                auto state(state_.lock());
                state->willSubstitute.insert(path);
                state->downloadSize += info->second.downloadSize;
                state->narSize += info->second.narSize;
            }

            for (auto & ref : info->second.references)
                pool.enqueue(std::bind(doPath, ref));
        }
    };

    for (auto & path : targets)
        pool.enqueue(std::bind(doPath, path));

    pool.process();
}


Paths Store::topoSortPaths(const PathSet & paths)
{
    Paths sorted;
    PathSet visited, parents;

    std::function<void(const Path & path, const Path * parent)> dfsVisit;

    dfsVisit = [&](const Path & path, const Path * parent) {
        if (parents.find(path) != parents.end())
            throw BuildError(format("cycle detected in the references of '%1%' from '%2%'") % path % *parent);

        if (visited.find(path) != visited.end()) return;
        visited.insert(path);
        parents.insert(path);

        PathSet references;
        try {
            references = queryPathInfo(path)->references;
        } catch (InvalidPath &) {
        }

        for (auto & i : references)
            /* Don't traverse into paths that don't exist.  That can
               happen due to substitutes for non-existent paths. */
            if (i != path && paths.find(i) != paths.end())
                dfsVisit(i, &path);

        sorted.push_front(path);
        parents.erase(path);
    };

    for (auto & i : paths)
        dfsVisit(i, nullptr);

    return sorted;
}


}
