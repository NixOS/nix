#include "normalise.hh"
#include "globals.hh"


void followLivePaths(Path nePath, PathSet & live)
{
    /* Just to be sure, canonicalise the path.  It is important to do
       this here and in findDeadPath() to ensure that a live path is
       not mistaken for a dead path due to some non-canonical
       representation. */
    nePath = canonPath(nePath);
    
    if (live.find(nePath) != live.end()) return;
    live.insert(nePath);

    startNest(nest, lvlDebug, format("following `%1%'") % nePath);
    assertStorePath(nePath);

    if (isValidPath(nePath)) {

        /* !!! should make sure that no substitutes are used */
        StoreExpr ne = storeExprFromPath(nePath);

        /* !!! painfully similar to requisitesWorker() */
        if (ne.type == StoreExpr::neClosure)
            for (ClosureElems::iterator i = ne.closure.elems.begin();
                 i != ne.closure.elems.end(); ++i)
            {
                Path p = canonPath(i->first);
                if (live.find(p) == live.end()) {
                    debug(format("found live `%1%'") % p);
                    assertStorePath(p);
                    live.insert(p);
                }
            }
    
        else if (ne.type == StoreExpr::neDerivation)
            for (PathSet::iterator i = ne.derivation.inputs.begin();
                 i != ne.derivation.inputs.end(); ++i)
                followLivePaths(*i, live);

        else abort();
        
    }

    Path nfPath;
    if (querySuccessor(nePath, nfPath))
        followLivePaths(nfPath, live);
}


PathSet findLivePaths(const Paths & roots)
{
    PathSet live;

    startNest(nest, lvlDebug, "finding live paths");

    for (Paths::const_iterator i = roots.begin(); i != roots.end(); ++i)
        followLivePaths(*i, live);

    return live;
}


PathSet findDeadPaths(const PathSet & live)
{
    PathSet dead;

    startNest(nest, lvlDebug, "finding dead paths");

    Strings storeNames = readDirectory(nixStore);

    for (Strings::iterator i = storeNames.begin(); i != storeNames.end(); ++i) {
        Path p = canonPath(nixStore + "/" + *i);
        if (live.find(p) == live.end()) {
            debug(format("dead path `%1%'") % p);
            dead.insert(p);
        } else
            debug(format("live path `%1%'") % p);
    }
    
    return dead;
}
