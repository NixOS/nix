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
   indirectly reached from it.  `paths' is not cleared. */
void computeFSClosure(const Path & storePath,
    PathSet & paths);

/* Place in `paths' the set of paths that are required to `realise'
   the given store path, i.e., all paths necessary for valid
   deployment of the path.  For a derivation, this is the union of
   requisites of the inputs, plus the derivation; for other store
   paths, it is the set of paths in the FS closure of the path.  If
   `includeOutputs' is true, include the requisites of the output
   paths of derivations as well.

   Note that this function can be used to implement three different
   deployment policies:

   - Source deployment (when called on a derivation).
   - Binary deployment (when called on an output path).
   - Source/binary deployment (when called on a derivation with
     `includeOutputs' set to true).
*/
void storePathRequisites(const Path & storePath,
    bool includeOutputs, PathSet & paths);

#endif /* !__BUILD_H */
