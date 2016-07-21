#include "derivations.hh"
#include "globals.hh"
#include "local-store.hh"
#include "store-api.hh"
#include "thread-pool.hh"


namespace nix {


void Store::computeFSClosure(const Path & path,
    PathSet & paths, bool flipDirection, bool includeOutputs, bool includeDerivers)
{
    ThreadPool pool;

    Sync<bool> state_;

    std::function<void(Path)> doPath;

    doPath = [&](const Path & path) {
        {
            auto state(state_.lock());
            if (paths.count(path)) return;
            paths.insert(path);
        }

        auto info = queryPathInfo(path);

        if (flipDirection) {

            PathSet referrers;
            queryReferrers(path, referrers);
            for (auto & ref : referrers)
                if (ref != path)
                    pool.enqueue(std::bind(doPath, ref));

            if (includeOutputs) {
                PathSet derivers = queryValidDerivers(path);
                for (auto & i : derivers)
                    pool.enqueue(std::bind(doPath, i));
            }

            if (includeDerivers && isDerivation(path)) {
                PathSet outputs = queryDerivationOutputs(path);
                for (auto & i : outputs)
                    if (isValidPath(i) && queryPathInfo(i)->deriver == path)
                        pool.enqueue(std::bind(doPath, i));
            }

        } else {

            for (auto & ref : info->references)
                if (ref != path)
                    pool.enqueue(std::bind(doPath, ref));

            if (includeOutputs && isDerivation(path)) {
                PathSet outputs = queryDerivationOutputs(path);
                for (auto & i : outputs)
                    if (isValidPath(i)) pool.enqueue(std::bind(doPath, i));
            }

            if (includeDerivers && isValidPath(info->deriver))
                pool.enqueue(std::bind(doPath, info->deriver));

        }
    };

    pool.enqueue(std::bind(doPath, path));

    pool.process();
}


void Store::queryMissing(const PathSet & targets,
    PathSet & willBuild_, PathSet & willSubstitute_, PathSet & unknown_,
    unsigned long long & downloadSize_, unsigned long long & narSize_)
{
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

            PathSet invalid;
            for (auto & j : drv.outputs)
                if (wantOutput(j.first, i2.second)
                    && !isValidPath(j.second.path))
                    invalid.insert(j.second.path);
            if (invalid.empty()) return;

            if (settings.useSubstitutes && drv.substitutesAllowed()) {
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
            throw BuildError(format("cycle detected in the references of ‘%1%’ from ‘%2%’") % path % *parent);

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
