#include "misc.hh"
#include "store-api.hh"
#include "local-store.hh"
#include "globals.hh"


namespace nix {


Derivation derivationFromPath(StoreAPI & store, const Path & drvPath)
{
    assertStorePath(drvPath);
    store.ensurePath(drvPath);
    return parseDerivation(readFile(drvPath));
}


void computeFSClosure(StoreAPI & store, const Path & path,
    PathSet & paths, bool flipDirection, bool includeOutputs, bool includeDerivers)
{
    if (paths.find(path) != paths.end()) return;
    paths.insert(path);

    PathSet edges;

    if (flipDirection) {
        store.queryReferrers(path, edges);

        if (includeOutputs) {
            PathSet derivers = store.queryValidDerivers(path);
            foreach (PathSet::iterator, i, derivers)
                edges.insert(*i);
        }

        if (includeDerivers && isDerivation(path)) {
            PathSet outputs = store.queryDerivationOutputs(path);
            foreach (PathSet::iterator, i, outputs)
                if (store.isValidPath(*i) && store.queryDeriver(*i) == path)
                    edges.insert(*i);
        }

    } else {
        store.queryReferences(path, edges);

        if (includeOutputs && isDerivation(path)) {
            PathSet outputs = store.queryDerivationOutputs(path);
            foreach (PathSet::iterator, i, outputs)
                if (store.isValidPath(*i)) edges.insert(*i);
        }

        if (includeDerivers) {
            Path deriver = store.queryDeriver(path);
            if (store.isValidPath(deriver)) edges.insert(deriver);
        }
    }

    foreach (PathSet::iterator, i, edges)
        computeFSClosure(store, *i, paths, flipDirection, includeOutputs, includeDerivers);
}


Path findOutput(const Derivation & drv, string id)
{
    foreach (DerivationOutputs::const_iterator, i, drv.outputs)
        if (i->first == id) return i->second.path;
    throw Error(format("derivation has no output `%1%'") % id);
}


void queryMissing(StoreAPI & store, const PathSet & targets,
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

        foreach (PathSet::iterator, i, todo) {
            if (done.find(*i) != done.end()) continue;
            done.insert(*i);

            DrvPathWithOutputs i2 = parseDrvPathWithOutputs(*i);

            if (isDerivation(i2.first)) {
                if (!store.isValidPath(i2.first)) {
                    // FIXME: we could try to substitute p.
                    unknown.insert(*i);
                    continue;
                }
                Derivation drv = derivationFromPath(store, i2.first);

                PathSet invalid;
                foreach (DerivationOutputs::iterator, j, drv.outputs)
                    if (wantOutput(j->first, i2.second)
                        && !store.isValidPath(j->second.path))
                        invalid.insert(j->second.path);
                if (invalid.empty()) continue;

                todoDrv.insert(*i);
                if (settings.useSubstitutes && !willBuildLocally(drv))
                    query.insert(invalid.begin(), invalid.end());
            }

            else {
                if (store.isValidPath(*i)) continue;
                query.insert(*i);
                todoNonDrv.insert(*i);
            }
        }

        todo.clear();

        SubstitutablePathInfos infos;
        store.querySubstitutablePathInfos(query, infos);

        foreach (PathSet::iterator, i, todoDrv) {
            DrvPathWithOutputs i2 = parseDrvPathWithOutputs(*i);

            // FIXME: cache this
            Derivation drv = derivationFromPath(store, i2.first);

            PathSet outputs;
            bool mustBuild = false;
            if (settings.useSubstitutes && !willBuildLocally(drv)) {
                foreach (DerivationOutputs::iterator, j, drv.outputs) {
                    if (!wantOutput(j->first, i2.second)) continue;
                    if (!store.isValidPath(j->second.path)) {
                        if (infos.find(j->second.path) == infos.end())
                            mustBuild = true;
                        else
                            outputs.insert(j->second.path);
                    }
                }
            } else
                mustBuild = true;

            if (mustBuild) {
                willBuild.insert(i2.first);
                todo.insert(drv.inputSrcs.begin(), drv.inputSrcs.end());
                foreach (DerivationInputs::iterator, j, drv.inputDrvs)
                    todo.insert(makeDrvPathWithOutputs(j->first, j->second));
            } else
                todoNonDrv.insert(outputs.begin(), outputs.end());
        }

        foreach (PathSet::iterator, i, todoNonDrv) {
            done.insert(*i);
            SubstitutablePathInfos::iterator info = infos.find(*i);
            if (info != infos.end()) {
                willSubstitute.insert(*i);
                downloadSize += info->second.downloadSize;
                narSize += info->second.narSize;
                todo.insert(info->second.references.begin(), info->second.references.end());
            } else
                unknown.insert(*i);
        }
    }
}


static void dfsVisit(StoreAPI & store, const PathSet & paths,
    const Path & path, PathSet & visited, Paths & sorted,
    PathSet & parents)
{
    if (parents.find(path) != parents.end())
        throw BuildError(format("cycle detected in the references of `%1%'") % path);

    if (visited.find(path) != visited.end()) return;
    visited.insert(path);
    parents.insert(path);

    PathSet references;
    if (store.isValidPath(path))
        store.queryReferences(path, references);

    foreach (PathSet::iterator, i, references)
        /* Don't traverse into paths that don't exist.  That can
           happen due to substitutes for non-existent paths. */
        if (*i != path && paths.find(*i) != paths.end())
            dfsVisit(store, paths, *i, visited, sorted, parents);

    sorted.push_front(path);
    parents.erase(path);
}


Paths topoSortPaths(StoreAPI & store, const PathSet & paths)
{
    Paths sorted;
    PathSet visited, parents;
    foreach (PathSet::const_iterator, i, paths)
        dfsVisit(store, paths, *i, visited, sorted, parents);
    return sorted;
}


}
