#ifndef __GC_H
#define __GC_H

#include "util.hh"


/* Garbage collector operation. */
typedef enum { gcReturnLive, gcReturnDead, gcDeleteDead } GCAction;

/* If `action' is set to `soReturnLive', return the set of paths
   reachable from (i.e. in the closure of) the specified roots.  If
   `action' is `soReturnDead', return the set of paths not reachable
   from the roots.  If `action' is `soDeleteDead', actually delete the
   latter set. */
void collectGarbage(const PathSet & roots, GCAction action,
    PathSet & result);

/* Register a temporary GC root.  This root will automatically
   disappear when this process exits.  WARNING: this function should
   not be called inside a BDB transaction, otherwise we can
   deadlock. */
void addTempRoot(const Path & path);


#endif /* !__GC_H */
