#ifndef __NORMALISE_H
#define __NORMALISE_H

#include "fstate.hh"


/* Normalise an fstate-expression, that is, return an equivalent
   Slice. */
Slice normaliseFState(FSId id);

/* Realise a Slice in the file system. */
void realiseSlice(const Slice & slice);

/* Get the list of root (output) paths of the given
   fstate-expression. */
Strings fstatePaths(const FSId & id, bool normalise);

/* Get the list of paths referenced by the given fstate-expression. */
StringSet fstateRefs(const FSId & id);

/* Register a successor. */
void registerSuccessor(const FSId & id1, const FSId & id2);


#endif /* !__NORMALISE_H */
