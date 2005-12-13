#ifndef __BUILD_H
#define __BUILD_H

#include "derivations.hh"

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

/* Read a derivation, after ensuring its existence through
   ensurePath(). */
Derivation derivationFromPath(const Path & drvPath);

/* Place in `paths' the set of all store paths in the file system
   closure of `storePath'; that is, all paths than can be directly or
   indirectly reached from it.  `paths' is not cleared.  If
   `flipDirection' is true, the set of paths that can reach
   `storePath' is returned; that is, the closures under the
   `referrers' relation instead of the `references' relation is
   returned. */
void computeFSClosure(const Path & storePath,
    PathSet & paths, bool flipDirection = false);

/* Return the path corresponding to the output identifier `id' in the
   given derivation. */
Path findOutput(const Derivation & drv, string id);
    

#endif /* !__BUILD_H */
