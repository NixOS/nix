#ifndef __NORMALISE_H
#define __NORMALISE_H

#include "storeexpr.hh"


/* Perform the specified derivation, if necessary.  That is, do
   whatever is necessary to create the output paths of the
   derivation.  If the output paths already exists, we're done.  If
   they have substitutes, we can use those instead.  Otherwise, the
   build action described by the derivation is performed, after
   recursively building any sub-derivations. */
void buildDerivation(const Path & drvPath);

/* Ensure that a path exists, possibly by instantiating it by
   realising a substitute. */
void ensurePath(const Path & storePath);

/* Read a derivation store expression, after ensuring its existence
   through ensurePath(). */
Derivation derivationFromPath(const Path & drvPath);


/* Places in `paths' the set of all store paths in the file system
   closure of `storePath'; that is, all paths than can be directly or
   indirectly reached from it.  `paths' is not cleared. */
void computeFSClosure(const Path & storePath,
    PathSet & paths);


#if 0
/* Get the list of root (output) paths of the given store
   expression. */
PathSet storeExprRoots(const Path & nePath);

/* Get the list of paths that are required to realise the given store
   expression.  For a derive expression, this is the union of
   requisites of the inputs; for a closure expression, it is the path
   of each element in the closure.  If `includeExprs' is true, include
   the paths of the store expressions themselves.  If
   `includeSuccessors' is true, include the requisites of
   successors. */
PathSet storeExprRequisites(const Path & nePath,
    bool includeExprs, bool includeSuccessors);
#endif


#endif /* !__NORMALISE_H */
