#ifndef __NORMALISE_H
#define __NORMALISE_H

#include "expr.hh"


/* Normalise a Nix expression, that is, return an equivalent
   closure.  (For the meaning of `pending', see expandId()). */
FSId normaliseNixExpr(FSId id, FSIdSet pending = FSIdSet());

/* Realise a Closure in the file system. */
void realiseClosure(const FSId & id, FSIdSet pending = FSIdSet());

/* Get the list of root (output) paths of the given Nix expression. */
Strings nixExprPaths(const FSId & id);

/* Get the list of paths that are required to realise the given
   expression.  For a derive expression, this is the union of
   requisites of the inputs; for a closure expression, it is the path of
   each element in the closure.  If `includeExprs' is true, include the
   paths of the Nix expressions themselves.  If `includeSuccessors' is
   true, include the requisites of successors. */
Strings nixExprRequisites(const FSId & id,
    bool includeExprs, bool includeSuccessors);

/* Return the list of the ids of all known Nix expressions whose
   output ids are completely contained in `ids'. */
FSIds findGenerators(const FSIds & ids);

/* Register a successor. */
void registerSuccessor(const Transaction & txn,
    const FSId & id1, const FSId & id2);


#endif /* !__NORMALISE_H */
