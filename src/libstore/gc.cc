#include "globals.hh"
#include "gc.hh"
#include "build.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


void collectGarbage(const PathSet & roots, GCAction action,
    PathSet & result)
{
    result.clear();
    
    /* !!! TODO: Acquire an exclusive lock on the gcroots directory.
       This prevents the set of live paths from increasing after this
       point. */
    
    /* Determine the live paths which is just the closure of the
       roots under the `references' relation. */
    PathSet livePaths;
    for (PathSet::const_iterator i = roots.begin(); i != roots.end(); ++i)
        computeFSClosure(canonPath(*i), livePaths);

    if (action == gcReturnLive) {
        result = livePaths;
        return;
    }

    /* !!! TODO: Try to acquire (without blocking) exclusive locks on
       the files in the `pending' directory.  Delete all files for
       which we managed to acquire such a lock (since if we could get
       such a lock, that means that the process that owned the file
       has died). */

    /* !!! TODO: Acquire shared locks on all files in the pending
       directories.  This prevents the set of pending paths from
       increasing while we are garbage-collecting.  Read the set of
       pending paths from those files. */

    /* Read the Nix store directory to find all currently existing
       paths. */
    Strings storeNames = readDirectory(nixStore);

    for (Strings::iterator i = storeNames.begin(); i != storeNames.end(); ++i) {
        Path path = canonPath(nixStore + "/" + *i);

        if (livePaths.find(path) != livePaths.end()) {
            debug(format("live path `%1%'") % path);
            continue;
        }

        debug(format("dead path `%1%'") % path);
        result.insert(path);

        if (action == gcDeleteDead) {
            printMsg(lvlInfo, format("deleting `%1%'") % path);
            deleteFromStore(path);
        }
        
    }
}



#if 0
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


PathSet findDeadPaths(const PathSet & live, time_t minAge)
{
    PathSet dead;

    startNest(nest, lvlDebug, "finding dead paths");

    time_t now = time(0);

    Strings storeNames = readDirectory(nixStore);

    for (Strings::iterator i = storeNames.begin(); i != storeNames.end(); ++i) {
        Path p = canonPath(nixStore + "/" + *i);

        if (minAge > 0) {
            struct stat st;
            if (lstat(p.c_str(), &st) != 0)
                throw SysError(format("obtaining information about `%1%'") % p);
            if (st.st_atime + minAge >= now) continue;
        }
        
        if (live.find(p) == live.end()) {
            debug(format("dead path `%1%'") % p);
            dead.insert(p);
        } else
            debug(format("live path `%1%'") % p);
    }
    
    return dead;
}
#endif
