#ifndef __GC_H
#define __GC_H

#include "types.hh"


namespace nix {


/* Garbage collector operation. */
typedef enum {
    gcReturnRoots,
    gcReturnLive,
    gcReturnDead,
    gcDeleteDead,
    gcDeleteSpecific,
} GCAction;

/* If `action' is set to `gcReturnRoots', find and return the set of
   roots for the garbage collector.  These are the store paths
   symlinked to in the `gcroots' directory.  If `action' is
   `gcReturnLive', return the set of paths reachable from (i.e. in the
   closure of) the roots.  If `action' is `gcReturnDead', return the
   set of paths not reachable from the roots.  If `action' is
   `gcDeleteDead', actually delete the latter set. */
void collectGarbage(GCAction action, const PathSet & pathsToDelete,
    bool ignoreLiveness, PathSet & result, unsigned long long & bytesFreed);

/* Register a temporary GC root.  This root will automatically
   disappear when this process exits.  WARNING: this function should
   not be called inside a BDB transaction, otherwise we can
   deadlock. */
void addTempRoot(const Path & path);

/* Remove the temporary roots file for this process.  Any temporary
   root becomes garbage after this point unless it has been registered
   as a (permanent) root. */
void removeTempRoots();

/* Register a permanent GC root. */
Path addPermRoot(const Path & storePath, const Path & gcRoot,
    bool indirect);


}


#endif /* !__GC_H */
