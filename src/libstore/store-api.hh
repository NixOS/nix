#ifndef __STOREAPI_H
#define __STOREAPI_H

#include "hash.hh"
#include "serialise.hh"

#include <string>
#include <map>

#include <boost/shared_ptr.hpp>


namespace nix {


typedef std::map<Path, Path> Roots;


struct GCOptions
{
    /* Garbage collector operation:

       - `gcReturnLive': return the set of paths reachable from
         (i.e. in the closure of) the roots.

       - `gcReturnDead': return the set of paths not reachable from
         the roots.

       - `gcDeleteDead': actually delete the latter set.

       - `gcDeleteSpecific': delete the paths listed in
          `pathsToDelete', insofar as they are not reachable.
    */
    typedef enum {
        gcReturnLive,
        gcReturnDead,
        gcDeleteDead,
        gcDeleteSpecific,
    } GCAction;

    GCAction action;

    /* If `ignoreLiveness' is set, then reachability from the roots is
       ignored (dangerous!).  However, the paths must still be
       unreferenced *within* the store (i.e., there can be no other
       store paths that depend on them). */
    bool ignoreLiveness;

    /* For `gcDeleteSpecific', the paths to delete. */
    PathSet pathsToDelete;

    /* Stop after at least `maxFreed' bytes have been freed.  0 means
       no limit. */
    unsigned long long maxFreed;

    /* Stop after the number of hard links to the Nix store directory
       has dropped below `maxLinks'. */
    unsigned int maxLinks;

    GCOptions();
};


struct GCResults 
{
    /* Depending on the action, the GC roots, or the paths that would
       be or have been deleted. */
    PathSet paths;

    /* For `gcReturnDead', `gcDeleteDead' and `gcDeleteSpecific', the
       number of bytes that would be or was freed. */
    unsigned long long bytesFreed;

    /* The number of file system blocks that would be or was freed. */
    unsigned long long blocksFreed;

    GCResults()
    {
        bytesFreed = 0;
        blocksFreed = 0;
    }
};


struct SubstitutablePathInfo
{
    Path deriver;
    PathSet references;
    unsigned long long downloadSize; /* 0 = unknown or inapplicable */
};


class StoreAPI 
{
public:

    virtual ~StoreAPI() { }

    /* Checks whether a path is valid. */ 
    virtual bool isValidPath(const Path & path) = 0;

    /* Query the set of valid paths. */
    virtual PathSet queryValidPaths() = 0;

    /* Queries the hash of a valid path. */ 
    virtual Hash queryPathHash(const Path & path) = 0;

    /* Queries the set of outgoing FS references for a store path.
       The result is not cleared. */
    virtual void queryReferences(const Path & path,
        PathSet & references) = 0;

    /* Like queryReferences, but with self-references filtered out. */
    PathSet queryReferencesNoSelf(const Path & path)
    {
        PathSet res;
        queryReferences(path, res);
        res.erase(path);
        return res;
    }

    /* Queries the set of incoming FS references for a store path.
       The result is not cleared. */
    virtual void queryReferrers(const Path & path,
        PathSet & referrers) = 0;

    /* Like queryReferrers, but with self-references filtered out. */
    PathSet queryReferrersNoSelf(const Path & path)
    {
        PathSet res;
        queryReferrers(path, res);
        res.erase(path);
        return res;
    }

    /* Query the deriver of a store path.  Return the empty string if
       no deriver has been set. */
    virtual Path queryDeriver(const Path & path) = 0;

    /* Query whether a path has substitutes. */
    virtual bool hasSubstitutes(const Path & path) = 0;

    /* Query the references, deriver and download size of a
       substitutable path. */
    virtual bool querySubstitutablePathInfo(const Path & path,
        SubstitutablePathInfo & info) = 0;
    
    /* Copy the contents of a path to the store and register the
       validity the resulting path.  The resulting path is returned.
       If `fixed' is true, then the output of a fixed-output
       derivation is pre-loaded into the Nix store.  The function
       object `filter' can be used to exclude files (see
       libutil/archive.hh). */
    virtual Path addToStore(const Path & srcPath,
        bool recursive = true, HashType hashAlgo = htSHA256,
        PathFilter & filter = defaultPathFilter) = 0;

    /* Like addToStore, but the contents written to the output path is
       a regular file containing the given string. */
    virtual Path addTextToStore(const string & name, const string & s,
        const PathSet & references) = 0;

    /* Export a store path, that is, create a NAR dump of the store
       path and append its references and its deriver.  Optionally, a
       cryptographic signature (created by OpenSSL) of the preceding
       data is attached. */
    virtual void exportPath(const Path & path, bool sign,
        Sink & sink) = 0;

    /* Import a NAR dump created by exportPath() into the Nix
       store. */
    virtual Path importPath(bool requireSignature, Source & source) = 0;

    /* Ensure that the output paths of the derivation are valid.  If
       they are already valid, this is a no-op.  Otherwise, validity
       can be reached in two ways.  First, if the output paths is
       substitutable, then build the path that way.  Second, the
       output paths can be created by running the builder, after
       recursively building any sub-derivations. */
    virtual void buildDerivations(const PathSet & drvPaths) = 0;

    /* Ensure that a path is valid.  If it is not currently valid, it
       may be made valid by running a substitute (if defined for the
       path). */
    virtual void ensurePath(const Path & path) = 0;

    /* Add a store path as a temporary root of the garbage collector.
       The root disappears as soon as we exit. */
    virtual void addTempRoot(const Path & path) = 0;

    /* Add an indirect root, which is merely a symlink to `path' from
       /nix/var/nix/gcroots/auto/<hash of `path'>.  `path' is supposed
       to be a symlink to a store path.  The garbage collector will
       automatically remove the indirect root when it finds that
       `path' has disappeared. */
    virtual void addIndirectRoot(const Path & path) = 0;

    /* Acquire the global GC lock, then immediately release it.  This
       function must be called after registering a new permanent root,
       but before exiting.  Otherwise, it is possible that a running
       garbage collector doesn't see the new root and deletes the
       stuff we've just built.  By acquiring the lock briefly, we
       ensure that either:

       - The collector is already running, and so we block until the
         collector is finished.  The collector will know about our
         *temporary* locks, which should include whatever it is we
         want to register as a permanent lock.

       - The collector isn't running, or it's just started but hasn't
         acquired the GC lock yet.  In that case we get and release
         the lock right away, then exit.  The collector scans the
         permanent root and sees our's.

       In either case the permanent root is seen by the collector. */
    virtual void syncWithGC() = 0;

    /* Find the roots of the garbage collector.  Each root is a pair
       (link, storepath) where `link' is the path of the symlink
       outside of the Nix store that point to `storePath'.  */
    virtual Roots findRoots() = 0;

    /* Perform a garbage collection. */
    virtual void collectGarbage(const GCOptions & options, GCResults & results) = 0;
};


/* !!! These should be part of the store API, I guess. */

/* Throw an exception if `path' is not directly in the Nix store. */
void assertStorePath(const Path & path);

bool isInStore(const Path & path);
bool isStorePath(const Path & path);

void checkStoreName(const string & name);


/* Chop off the parts after the top-level store name, e.g.,
   /nix/store/abcd-foo/bar => /nix/store/abcd-foo. */
Path toStorePath(const Path & path);


/* Get the "name" part of a store path, that is, the part after the
   hash and the dash, and with any ".drv" suffix removed
   (e.g. /nix/store/<hash>-foo-1.2.3.drv => foo-1.2.3). */
string getNameOfStorePath(const Path & path);


/* Follow symlinks until we end up with a path in the Nix store. */
Path followLinksToStore(const Path & path);


/* Same as followLinksToStore(), but apply toStorePath() to the
   result. */
Path followLinksToStorePath(const Path & path);


/* Constructs a unique store path name. */
Path makeStorePath(const string & type,
    const Hash & hash, const string & name);
    
Path makeFixedOutputPath(bool recursive,
    HashType hashAlgo, Hash hash, string name);


/* This is the preparatory part of addToStore() and addToStoreFixed();
   it computes the store path to which srcPath is to be copied.
   Returns the store path and the cryptographic hash of the
   contents of srcPath. */
std::pair<Path, Hash> computeStorePathForPath(const Path & srcPath,
    bool recursive = true, HashType hashAlgo = htSHA256,
    PathFilter & filter = defaultPathFilter);

/* Preparatory part of addTextToStore().

   !!! Computation of the path should take the references given to
   addTextToStore() into account, otherwise we have a (relatively
   minor) security hole: a caller can register a source file with
   bogus references.  If there are too many references, the path may
   not be garbage collected when it has to be (not really a problem,
   the caller could create a root anyway), or it may be garbage
   collected when it shouldn't be (more serious).

   Hashing the references would solve this (bogus references would
   simply yield a different store path, so other users wouldn't be
   affected), but it has some backwards compatibility issues (the
   hashing scheme changes), so I'm not doing that for now. */
Path computeStorePathForText(const string & name, const string & s,
    const PathSet & references);


/* Remove the temporary roots file for this process.  Any temporary
   root becomes garbage after this point unless it has been registered
   as a (permanent) root. */
void removeTempRoots();


/* Register a permanent GC root. */
Path addPermRoot(const Path & storePath, const Path & gcRoot,
    bool indirect, bool allowOutsideRootsDir = false);


/* Sort a set of paths topologically under the references relation.
   If p refers to q, then p follows q in this list. */
Paths topoSortPaths(const PathSet & paths);


/* For now, there is a single global store API object, but we'll
   purify that in the future. */
extern boost::shared_ptr<StoreAPI> store;


/* Factory method: open the Nix database, either through the local or
   remote implementation. */
boost::shared_ptr<StoreAPI> openStore();


/* Display a set of paths in human-readable form (i.e., between quotes
   and separated by commas). */
string showPaths(const PathSet & paths);


string makeValidityRegistration(const PathSet & paths,
    bool showDerivers, bool showHash);
    
struct ValidPathInfo 
{
    Path path;
    Path deriver;
    Hash hash;
    PathSet references;
    time_t registrationTime;
    ValidPathInfo() : registrationTime(0) { }
};

typedef list<ValidPathInfo> ValidPathInfos;

ValidPathInfo decodeValidPathInfo(std::istream & str,
    bool hashGiven = false);


}


#endif /* !__STOREAPI_H */
