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
Strings fstateRefs(const FSId & id);

/* Return the list of the ids of all known fstate-expressions whose
   output ids are completely contained in `ids'. */
FSIds findGenerators(const FSIds & ids);

/* Register a successor. */
void registerSuccessor(const FSId & id1, const FSId & id2);


#endif /* !__NORMALISE_H */
