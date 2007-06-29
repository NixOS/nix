#ifndef __MISC_H
#define __MISC_H

#include "derivations.hh"


namespace nix {


/* Read a derivation, after ensuring its existence through
   ensurePath(). */
Derivation derivationFromPath(const Path & drvPath);

/* Place in `paths' the set of all store paths in the file system
   closure of `storePath'; that is, all paths than can be directly or
   indirectly reached from it.  `paths' is not cleared.  If
   `flipDirection' is true, the set of paths that can reach
   `storePath' is returned; that is, the closures under the
   `referrers' relation instead of the `references' relation is
   
   withState = TODO comment
   withComponents = TODO
   
   returned. */
void computeFSClosure(const Path & storePath, PathSet & paths, const bool & withComponents, const bool & withState, bool flipDirection = false);

void computeFSClosureRec(const Path & path, PathSet & paths, const bool & flipDirection);	//private    

/* Return the path corresponding to the output identifier `id' in the
   given derivation. */
Path findOutput(const Derivation & drv, string id);

/* Given a set of paths that are to be built, return the set of
   derivations that will be built, and the set of output paths that
   will be substituted. */
void queryMissing(const PathSet & targets,
    PathSet & willBuild, PathSet & willSubstitute);


}


#endif /* !__MISC_H */
