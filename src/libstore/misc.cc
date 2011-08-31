#include "misc.hh"
#include "store-api.hh"
#include "local-store.hh"


namespace nix {


Derivation derivationFromPath(StoreAPI & store, const Path & drvPath)
{
    assertStorePath(drvPath);
    store.ensurePath(drvPath);
    return parseDerivation(readFile(drvPath));
}


void computeFSClosure(StoreAPI & store, const Path & storePath,
    PathSet & paths, bool flipDirection, bool includeOutputs)
{
    if (paths.find(storePath) != paths.end()) return;
    paths.insert(storePath);

    PathSet references;
    if (flipDirection)
        store.queryReferrers(storePath, references);
    else
        store.queryReferences(storePath, references);

    if (includeOutputs && isDerivation(storePath)) {
        PathSet outputs = store.queryDerivationOutputs(storePath);
        foreach (PathSet::iterator, i, outputs)
            if (store.isValidPath(*i))
                computeFSClosure(store, *i, paths, flipDirection, true);
    }

    foreach (PathSet::iterator, i, references)
        computeFSClosure(store, *i, paths, flipDirection, includeOutputs);
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

    while (!todo.empty()) {
        Path p = *(todo.begin());
        todo.erase(p);
        if (done.find(p) != done.end()) continue;
        done.insert(p);

        if (isDerivation(p)) {
            if (!store.isValidPath(p)) {
                unknown.insert(p);
                continue;
            }
            Derivation drv = derivationFromPath(store, p);

            bool mustBuild = false;
            foreach (DerivationOutputs::iterator, i, drv.outputs)
                if (!store.isValidPath(i->second.path) && !store.hasSubstitutes(i->second.path))
                    mustBuild = true;

            if (mustBuild) {
                willBuild.insert(p);
                todo.insert(drv.inputSrcs.begin(), drv.inputSrcs.end());
                foreach (DerivationInputs::iterator, i, drv.inputDrvs)
                    todo.insert(i->first);
            } else 
                foreach (DerivationOutputs::iterator, i, drv.outputs)
                    todo.insert(i->second.path);
        }

        else {
            if (store.isValidPath(p)) continue;
            SubstitutablePathInfo info;
            if (store.querySubstitutablePathInfo(p, info)) {
                willSubstitute.insert(p);
                downloadSize += info.downloadSize;
                narSize += info.narSize;
                todo.insert(info.references.begin(), info.references.end());
            } else
                unknown.insert(p);
        }
    }
}

 
}
