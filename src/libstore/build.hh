#ifndef __BUILD_H
#define __BUILD_H

#include "derivations.hh"

/* Perform the specified derivations, if necessary.  That is, do
   whatever is necessary to create the output paths of the derivation.
   If the output paths already exists, we're done.  If they have
   substitutes, we can use those instead.  Otherwise, the build action
   described by the derivation is performed, after recursively
   building any sub-derivations. */
void buildDerivations(const PathSet & drvPaths);

/* Ensure that a path exists, possibly by instantiating it by
   realising a substitute. */
void ensurePath(const Path & storePath);

/* Read a derivation store expression, after ensuring its existence
   through ensurePath(). */
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
