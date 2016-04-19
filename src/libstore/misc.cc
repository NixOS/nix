#include "derivations.hh"
#include "globals.hh"
#include "local-store.hh"
#include "store-api.hh"


namespace nix {


Derivation Store::derivationFromPath(const Path & drvPath)
{
    assertStorePath(drvPath);
    ensurePath(drvPath);
    return readDerivation(drvPath);
}


void Store::computeFSClosure(const Path & path,
    PathSet & paths, bool flipDirection, bool includeOutputs, bool includeDerivers)
{
    if (paths.find(path) != paths.end()) return;
    paths.insert(path);

    PathSet edges;

    if (flipDirection) {
        queryReferrers(path, edges);

        if (includeOutputs) {
            PathSet derivers = queryValidDerivers(path);
            for (auto & i : derivers)
                edges.insert(i);
        }

        if (includeDerivers && isDerivation(path)) {
            PathSet outputs = queryDerivationOutputs(path);
            for (auto & i : outputs)
                if (isValidPath(i) && queryPathInfo(i)->deriver == path)
                    edges.insert(i);
        }

    } else {
        auto info = queryPathInfo(path);
        edges = info->references;

        if (includeOutputs && isDerivation(path)) {
            PathSet outputs = queryDerivationOutputs(path);
            for (auto & i : outputs)
                if (isValidPath(i)) edges.insert(i);
        }

        if (includeDerivers && isValidPath(info->deriver))
            edges.insert(info->deriver);
    }

    for (auto & i : edges)
        computeFSClosure(i, paths, flipDirection, includeOutputs, includeDerivers);
}


void Store::queryMissing(const PathSet & targets,
    PathSet & willBuild, PathSet & willSubstitute, PathSet & unknown,
    unsigned long long & downloadSize, unsigned long long & narSize)
{
    downloadSize = narSize = 0;

    PathSet todo(targets.begin(), targets.end()), done;

    /* Getting substitute info has high latency when using the binary
       cache substituter.  Thus it's essential to do substitute
       queries in parallel as much as possible.  To accomplish this
       we do the following:

       - For all paths still to be processed (‘todo’), we add all
         paths for which we need info to the set ‘query’.  For an
         unbuilt derivation this is the output paths; otherwise, it's
         the path itself.

       - We get info about all paths in ‘query’ in parallel.

       - We process the results and add new items to ‘todo’ if
         necessary.  E.g. if a path is substitutable, then we need to
         get info on its references.

       - Repeat until ‘todo’ is empty.
    */

    while (!todo.empty()) {

        PathSet query, todoDrv, todoNonDrv;

        for (auto & i : todo) {
            if (done.find(i) != done.end()) continue;
            done.insert(i);

            DrvPathWithOutputs i2 = parseDrvPathWithOutputs(i);

            if (isDerivation(i2.first)) {
                if (!isValidPath(i2.first)) {
                    // FIXME: we could try to substitute p.
                    unknown.insert(i);
                    continue;
                }
                Derivation drv = derivationFromPath(i2.first);

                PathSet invalid;
                for (auto & j : drv.outputs)
                    if (wantOutput(j.first, i2.second)
                        && !isValidPath(j.second.path))
                        invalid.insert(j.second.path);
                if (invalid.empty()) continue;

                todoDrv.insert(i);
                if (settings.useSubstitutes && drv.substitutesAllowed())
                    query.insert(invalid.begin(), invalid.end());
            }

            else {
                if (isValidPath(i)) continue;
                query.insert(i);
                todoNonDrv.insert(i);
            }
        }

        todo.clear();

        SubstitutablePathInfos infos;
        querySubstitutablePathInfos(query, infos);

        for (auto & i : todoDrv) {
            DrvPathWithOutputs i2 = parseDrvPathWithOutputs(i);

            // FIXME: cache this
            Derivation drv = derivationFromPath(i2.first);

            PathSet outputs;
            bool mustBuild = false;
            if (settings.useSubstitutes && drv.substitutesAllowed()) {
                for (auto & j : drv.outputs) {
                    if (!wantOutput(j.first, i2.second)) continue;
                    if (!isValidPath(j.second.path)) {
                        if (infos.find(j.second.path) == infos.end())
                            mustBuild = true;
                        else
                            outputs.insert(j.second.path);
                    }
                }
            } else
                mustBuild = true;

            if (mustBuild) {
                willBuild.insert(i2.first);
                todo.insert(drv.inputSrcs.begin(), drv.inputSrcs.end());
                for (auto & j : drv.inputDrvs)
                    todo.insert(makeDrvPathWithOutputs(j.first, j.second));
            } else
                todoNonDrv.insert(outputs.begin(), outputs.end());
        }

        for (auto & i : todoNonDrv) {
            done.insert(i);
            SubstitutablePathInfos::iterator info = infos.find(i);
            if (info != infos.end()) {
                willSubstitute.insert(i);
                downloadSize += info->second.downloadSize;
                narSize += info->second.narSize;
                todo.insert(info->second.references.begin(), info->second.references.end());
            } else
                unknown.insert(i);
        }
    }
}


Paths Store::topoSortPaths(const PathSet & paths)
{
    Paths sorted;
    PathSet visited, parents;

    std::function<void(const Path & path)> dfsVisit;

    dfsVisit = [&](const Path & path) {
        if (parents.find(path) != parents.end())
            throw BuildError(format("cycle detected in the references of ‘%1%’") % path);

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
                dfsVisit(i);

        sorted.push_front(path);
        parents.erase(path);
    };

    for (auto & i : paths)
        dfsVisit(i);

    return sorted;
}


}
