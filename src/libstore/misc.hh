#ifndef __MISC_H
#define __MISC_H

#include "derivations.hh"
#include "store.hh"


/* Read a derivation, after ensuring its existence through
   ensurePath(). */
Derivation derivationFromPath(const Path & drvPath);


/* Place in `paths' the set of all store paths in the file system
   closure of `storePath'; that is, all paths than can be directly or
   indirectly reached from it.  `paths' is not cleared.  If
   `flipDirection' is true, the set of paths that can reach
   `storePath' is returned; that is, the closures under the `referers'
   relation instead of the `references' relation is returned. */
void computeFSClosure(const Path & storePath,
    PathSet & paths, bool flipDirection = false);


/* Return the output equivalence class denoted by `id' in the
   derivation `drv'. */
OutputEqClass findOutputEqClass(const Derivation & drv,
    const string & id);


/* Return any trusted path (wrt to the given trust ID) in the given
   output path equivalence class, or "" if no such path currently
   exists. */
Path findTrustedEqClassMember(const OutputEqClass & eqClass,
    const TrustId & trustId);


PathSet consolidatePaths(const PathSet & paths, bool checkOnly);


#endif /* !__MISC_H */
