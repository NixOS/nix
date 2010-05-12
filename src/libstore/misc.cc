#include "misc.hh"
#include "store-api.hh"
#include "local-store.hh"


namespace nix {


Derivation derivationFromPath(const Path & drvPath)
{
    assertStorePath(drvPath);
    store->ensurePath(drvPath);
    return parseDerivation(readFile(drvPath));
}


void computeFSClosure(const Path & storePath,
    PathSet & paths, bool flipDirection, bool includeOutputs)
{
    if (paths.find(storePath) != paths.end()) return;
    paths.insert(storePath);

    PathSet references;
    if (flipDirection)
        store->queryReferrers(storePath, references);
    else
        store->queryReferences(storePath, references);

    if (includeOutputs && isDerivation(storePath)) {
        Derivation drv = derivationFromPath(storePath);
        foreach (DerivationOutputs::iterator, i, drv.outputs)
            if (store->isValidPath(i->second.path))
                computeFSClosure(i->second.path, paths, flipDirection, true);
    }

    foreach (PathSet::iterator, i, references)
        computeFSClosure(*i, paths, flipDirection, includeOutputs);
}


Path findOutput(const Derivation & drv, string id)
{
    foreach (DerivationOutputs::const_iterator, i, drv.outputs)
        if (i->first == id) return i->second.path;
    throw Error(format("derivation has no output `%1%'") % id);
}


void queryMissing(const PathSet & targets,
    PathSet & willBuild, PathSet & willSubstitute, PathSet & unknown,
    unsigned long long & downloadSize)
{
    downloadSize = 0;
    
    PathSet todo(targets.begin(), targets.end()), done;

    while (!todo.empty()) {
        Path p = *(todo.begin());
        todo.erase(p);
        if (done.find(p) != done.end()) continue;
        done.insert(p);

        if (isDerivation(p)) {
            if (!store->isValidPath(p)) {
                unknown.insert(p);
                continue;
            }
            Derivation drv = derivationFromPath(p);

            bool mustBuild = false;
            foreach (DerivationOutputs::iterator, i, drv.outputs)
                if (!store->isValidPath(i->second.path) && !store->hasSubstitutes(i->second.path))
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
            if (store->isValidPath(p)) continue;
            SubstitutablePathInfo info;
            if (store->querySubstitutablePathInfo(p, info)) {
                willSubstitute.insert(p);
                downloadSize += info.downloadSize;
                todo.insert(info.references.begin(), info.references.end());
            } else
                unknown.insert(p);
        }
    }
}

 
}
