#ifndef __NORMALISE_H
#define __NORMALISE_H

#include "storeexpr.hh"


/* Normalise a store expression.  That is, if the expression is a
   derivation, a path containing an equivalent closure expression is
   returned.  This requires that the derivation is performed, unless a
   successor is known. */
Path normaliseStoreExpr(const Path & nePath);

/* Realise a closure store expression in the file system. 

   The pending paths are those that are already being realised.  This
   prevents infinite recursion for paths realised through a substitute
   (since when we build the substitute, we would first try to realise
   its output paths through substitutes... kaboom!). */
void realiseClosure(const Path & nePath);

/* Ensure that a path exists, possibly by instantiating it by
   realising a substitute. */
void ensurePath(const Path & path);

/* Read a store expression, after ensuring its existence through
   ensurePath(). */
StoreExpr storeExprFromPath(const Path & path);

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

/* Return the list of the paths of all known store expressions whose
   output paths are completely contained in the set `outputs'. */
PathSet findGenerators(const PathSet & outputs);


#endif /* !__NORMALISE_H */
