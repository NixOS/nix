#include "normalise.hh"


Derivation derivationFromPath(const Path & drvPath)
{
    assertStorePath(drvPath);
    ensurePath(drvPath);
    ATerm t = ATreadFromNamedFile(drvPath.c_str());
    if (!t) throw Error(format("cannot read aterm from `%1%'") % drvPath);
    return parseDerivation(t);
}


void computeFSClosure(const Path & storePath,
    PathSet & paths)
{
    if (paths.find(storePath) != paths.end()) return;
    paths.insert(storePath);

    PathSet references;
    queryReferences(storePath, references);

    for (PathSet::iterator i = references.begin();
         i != references.end(); ++i)
        computeFSClosure(*i, paths);
}


#if 0
PathSet storeExprRoots(const Path & nePath)
{
    PathSet paths;

    StoreExpr ne = storeExprFromPath(nePath);

    if (ne.type == StoreExpr::neClosure)
        paths.insert(ne.closure.roots.begin(), ne.closure.roots.end());
    else if (ne.type == StoreExpr::neDerivation)
        for (DerivationOutputs::iterator i = ne.derivation.outputs.begin();
             i != ne.derivation.outputs.end(); ++i)
            paths.insert(i->second.path);
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
#endif
