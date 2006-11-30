#include "misc.hh"
#include "store-api.hh"
#include "db.hh"

#include <aterm2.h>


namespace nix {


Derivation derivationFromPath(const Path & drvPath)
{
    assertStorePath(drvPath);
    store->ensurePath(drvPath);
    ATerm t = ATreadFromNamedFile(drvPath.c_str());
    if (!t) throw Error(format("cannot read aterm from `%1%'") % drvPath);
    return parseDerivation(t);
}


void computeFSClosure(const Path & storePath,
    PathSet & paths, bool flipDirection)
{
    if (paths.find(storePath) != paths.end()) return;
    paths.insert(storePath);

    PathSet references;
    if (flipDirection)
        store->queryReferrers(storePath, references);
    else
        store->queryReferences(storePath, references);

    for (PathSet::iterator i = references.begin();
         i != references.end(); ++i)
        computeFSClosure(*i, paths, flipDirection);
}


Path findOutput(const Derivation & drv, string id)
{
    for (DerivationOutputs::const_iterator i = drv.outputs.begin();
         i != drv.outputs.end(); ++i)
        if (i->first == id) return i->second.path;
    throw Error(format("derivation has no output `%1%'") % id);
}


void queryMissing(const PathSet & targets,
    PathSet & willBuild, PathSet & willSubstitute)
{
    PathSet todo(targets.begin(), targets.end()), done;

    while (!todo.empty()) {
        Path p = *(todo.begin());
        todo.erase(p);
        if (done.find(p) != done.end()) continue;
        done.insert(p);

        if (isDerivation(p)) {
            if (!store->isValidPath(p)) continue;
            Derivation drv = derivationFromPath(p);

            bool mustBuild = false;
            for (DerivationOutputs::iterator i = drv.outputs.begin();
                 i != drv.outputs.end(); ++i)
                if (!store->isValidPath(i->second.path) &&
                    store->querySubstitutes(i->second.path).size() == 0)
                    mustBuild = true;

            if (mustBuild) {
                willBuild.insert(p);
                todo.insert(drv.inputSrcs.begin(), drv.inputSrcs.end());
                for (DerivationInputs::iterator i = drv.inputDrvs.begin();
                     i != drv.inputDrvs.end(); ++i)
                    todo.insert(i->first);
            } else 
                for (DerivationOutputs::iterator i = drv.outputs.begin();
                     i != drv.outputs.end(); ++i)
                    todo.insert(i->second.path);
        }

        else {
            if (store->isValidPath(p)) continue;
            if (store->querySubstitutes(p).size() > 0)
                willSubstitute.insert(p);
            PathSet refs;
            store->queryReferences(p, todo);
        }
    }
}

 
}
