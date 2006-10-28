#ifndef __BUILD_H
#define __BUILD_H


#include "types.hh"


namespace nix {

    
extern string drvsLogDir;


/* Ensure that the output paths of the derivation are valid.  If they
   are already valid, this is a no-op.  Otherwise, validity can
   be reached in two ways.  First, if the output paths have
   substitutes, then those can be used.  Second, the output paths can
   be created by running the builder, after recursively building any
   sub-derivations. */
void buildDerivations(const PathSet & drvPaths);

/* Ensure that a path is valid.  If it is not currently valid, it may
   be made valid by running a substitute (if defined for the path). */
void ensurePath(const Path & storePath);

    
}


#endif /* !__BUILD_H */
