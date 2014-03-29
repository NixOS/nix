#pragma once

#include "hash.hh"
#include "serialise.hh"

#include <string>
#include <map>
#include <memory>


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

    /* Stop after at least `maxFreed' bytes have been freed. */
    unsigned long long maxFreed;

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

    GCResults()
    {
        bytesFreed = 0;
    }
};


struct SubstitutablePathInfo
{
    Path deriver;
    PathSet references;
    unsigned long long downloadSize; /* 0 = unknown or inapplicable */
    unsigned long long narSize; /* 0 = unknown */
};

typedef std::map<Path, SubstitutablePathInfo> SubstitutablePathInfos;


struct ValidPathInfo 
{
    Path path;
    Path deriver;
    Hash hash;
    PathSet references;
    time_t registrationTime;
    unsigned long long narSize; // 0 = unknown
    unsigned long long id; // internal use only
    ValidPathInfo() : registrationTime(0), narSize(0) { }
};

typedef list<ValidPathInfo> ValidPathInfos;


enum BuildMode { bmNormal, bmRepair, bmCheck };


class StoreAPI 
{
public:

    virtual ~StoreAPI() { }

    /* Check whether a path is valid. */ 
    virtual bool isValidPath(const Path & path) = 0;

    /* Query which of the given paths is valid. */
    virtual PathSet queryValidPaths(const PathSet & paths) = 0;

    /* Query the set of all valid paths. */
    virtual PathSet queryAllValidPaths() = 0;

    /* Query information about a valid path. */
    virtual ValidPathInfo queryPathInfo(const Path & path) = 0;

    /* Query the hash of a valid path. */ 
    virtual Hash queryPathHash(const Path & path) = 0;

    /* Query the set of outgoing FS references for a store path.  The
       result is not cleared. */
    virtual void queryReferences(const Path & path,
        PathSet & references) = 0;

    /* Queries the set of incoming FS references for a store path.
       The result is not cleared. */
    virtual void queryReferrers(const Path & path,
        PathSet & referrers) = 0;

    /* Query the deriver of a store path.  Return the empty string if
       no deriver has been set. */
    virtual Path queryDeriver(const Path & path) = 0;

    /* Return all currently valid derivations that have `path' as an
       output.  (Note that the result of `queryDeriver()' is the
       derivation that was actually used to produce `path', which may
       not exist anymore.) */
    virtual PathSet queryValidDerivers(const Path & path) = 0;

    /* Query the outputs of the derivation denoted by `path'. */
    virtual PathSet queryDerivationOutputs(const Path & path) = 0;

    /* Query the output names of the derivation denoted by `path'. */
    virtual StringSet queryDerivationOutputNames(const Path & path) = 0;

    /* Query the full store path given the hash part of a valid store
       path, or "" if the path doesn't exist. */
    virtual Path queryPathFromHashPart(const string & hashPart) = 0;
    
    /* Query which of the given paths have substitutes. */
    virtual PathSet querySubstitutablePaths(const PathSet & paths) = 0;

    /* Query substitute info (i.e. references, derivers and download
       sizes) of a set of paths.  If a path does not have substitute
       info, it's omitted from the resulting ‘infos’ map. */
    virtual void querySubstitutablePathInfos(const PathSet & paths,
        SubstitutablePathInfos & infos) = 0;
    
    /* Copy the contents of a path to the store and register the
       validity the resulting path.  The resulting path is returned.
       The function object `filter' can be used to exclude files (see
       libutil/archive.hh). */
    virtual Path addToStore(const Path & srcPath,
        bool recursive = true, HashType hashAlgo = htSHA256,
        PathFilter & filter = defaultPathFilter, bool repair = false) = 0;

    /* Like addToStore, but the contents written to the output path is
       a regular file containing the given string. */
    virtual Path addTextToStore(const string & name, const string & s,
        const PathSet & references, bool repair = false) = 0;

    /* Export a store path, that is, create a NAR dump of the store
       path and append its references and its deriver.  Optionally, a
       cryptographic signature (created by OpenSSL) of the preceding
       data is attached. */
    virtual void exportPath(const Path & path, bool sign,
        Sink & sink) = 0;

    /* Import a sequence of NAR dumps created by exportPaths() into
       the Nix store. */
    virtual Paths importPaths(bool requireSignature, Source & source) = 0;

    /* For each path, if it's a derivation, build it.  Building a
       derivation means ensuring that the output paths are valid.  If
       they are already valid, this is a no-op.  Otherwise, validity
       can be reached in two ways.  First, if the output paths is
       substitutable, then build the path that way.  Second, the
       output paths can be created by running the builder, after
       recursively building any sub-derivations. For inputs that are
       not derivations, substitute them. */
    virtual void buildPaths(const PathSet & paths, BuildMode buildMode = bmNormal) = 0;

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

    /* Return the set of paths that have failed to build.*/
    virtual PathSet queryFailedPaths() = 0;

    /* Clear the "failed" status of the given paths.  The special
       value `*' causes all failed paths to be cleared. */
    virtual void clearFailedPaths(const PathSet & paths) = 0;

    /* Return a string representing information about the path that
       can be loaded into the database using `nix-store --load-db' or
       `nix-store --register-validity'. */
    string makeValidityRegistration(const PathSet & paths,
        bool showDerivers, bool showHash);
};


/* !!! These should be part of the store API, I guess. */

/* Throw an exception if `path' is not directly in the Nix store. */
void assertStorePath(const Path & path);

bool isInStore(const Path & path);
bool isStorePath(const Path & path);

/* Extract the name part of the given store path. */
string storePathToName(const Path & path);
    
void checkStoreName(const string & name);


/* Chop off the parts after the top-level store name, e.g.,
   /nix/store/abcd-foo/bar => /nix/store/abcd-foo. */
Path toStorePath(const Path & path);


/* Follow symlinks until we end up with a path in the Nix store. */
Path followLinksToStore(const Path & path);


/* Same as followLinksToStore(), but apply toStorePath() to the
   result. */
Path followLinksToStorePath(const Path & path);


/* Constructs a unique store path name. */
Path makeStorePath(const string & type,
    const Hash & hash, const string & name);
    
Path makeOutputPath(const string & id,
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
Path addPermRoot(StoreAPI & store, const Path & storePath,
    const Path & gcRoot, bool indirect, bool allowOutsideRootsDir = false);


/* Sort a set of paths topologically under the references relation.
   If p refers to q, then p preceeds q in this list. */
Paths topoSortPaths(StoreAPI & store, const PathSet & paths);


/* For now, there is a single global store API object, but we'll
   purify that in the future. */
extern std::shared_ptr<StoreAPI> store;


/* Factory method: open the Nix database, either through the local or
   remote implementation. */
std::shared_ptr<StoreAPI> openStore(bool reserveSpace = true);


/* Display a set of paths in human-readable form (i.e., between quotes
   and separated by commas). */
string showPaths(const PathSet & paths);


ValidPathInfo decodeValidPathInfo(std::istream & str,
    bool hashGiven = false);


/* Export multiple paths in the format expected by ‘nix-store
   --import’. */
void exportPaths(StoreAPI & store, const Paths & paths,
    bool sign, Sink & sink);


MakeError(SubstError, Error)
MakeError(BuildError, Error) /* denotes a permanent build failure */


}
