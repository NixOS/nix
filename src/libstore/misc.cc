#include "normalise.hh"


StoreExpr storeExprFromPath(const Path & path)
{
    assertStorePath(path);
    ensurePath(path);
    ATerm t = ATreadFromNamedFile(path.c_str());
    if (!t) throw Error(format("cannot read aterm from `%1%'") % path);
    return parseStoreExpr(t);
}


PathSet storeExprRoots(const Path & nePath)
{
    PathSet paths;

    StoreExpr ne = storeExprFromPath(nePath);

    if (ne.type == StoreExpr::neClosure)
        paths.insert(ne.closure.roots.begin(), ne.closure.roots.end());
    else if (ne.type == StoreExpr::neDerivation)
        paths.insert(ne.derivation.outputs.begin(),
            ne.derivation.outputs.end());
    else abort();

    return paths;
}


static void requisitesWorker(const Path & nePath,
    bool includeExprs, bool includeSuccessors,
    PathSet & paths, PathSet & doneSet)
{
    checkInterrupt();
    
    if (doneSet.find(nePath) != doneSet.end()) return;
    doneSet.insert(nePath);

    StoreExpr ne = storeExprFromPath(nePath);

    if (ne.type == StoreExpr::neClosure)
        for (ClosureElems::iterator i = ne.closure.elems.begin();
             i != ne.closure.elems.end(); ++i)
            paths.insert(i->first);
    
    else if (ne.type == StoreExpr::neDerivation)
        for (PathSet::iterator i = ne.derivation.inputs.begin();
             i != ne.derivation.inputs.end(); ++i)
            requisitesWorker(*i,
                includeExprs, includeSuccessors, paths, doneSet);

    else abort();

    if (includeExprs) paths.insert(nePath);

    Path nfPath;
    if (includeSuccessors && querySuccessor(nePath, nfPath))
        requisitesWorker(nfPath, includeExprs, includeSuccessors,
            paths, doneSet);
}


PathSet storeExprRequisites(const Path & nePath,
    bool includeExprs, bool includeSuccessors)
{
    PathSet paths;
    PathSet doneSet;
    requisitesWorker(nePath, includeExprs, includeSuccessors,
        paths, doneSet);
    return paths;
}
