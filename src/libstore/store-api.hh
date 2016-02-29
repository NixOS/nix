#pragma once

#include "hash.hh"
#include "serialise.hh"

#include <string>
#include <limits>
#include <map>
#include <memory>


namespace nix {


/* Size of the hash part of store paths, in base-32 characters. */
const size_t storePathHashLen = 32; // i.e. 160 bits

/* Magic header of exportPath() output. */
const uint32_t exportMagic = 0x4558494e;


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

    GCAction action{gcDeleteDead};

    /* If `ignoreLiveness' is set, then reachability from the roots is
       ignored (dangerous!).  However, the paths must still be
       unreferenced *within* the store (i.e., there can be no other
       store paths that depend on them). */
    bool ignoreLiveness{false};

    /* For `gcDeleteSpecific', the paths to delete. */
    PathSet pathsToDelete;

    /* Stop after at least `maxFreed' bytes have been freed. */
    unsigned long long maxFreed{std::numeric_limits<unsigned long long>::max()};
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
    Hash narHash;
    PathSet references;
    time_t registrationTime = 0;
    unsigned long long narSize = 0; // 0 = unknown
    unsigned long long id; // internal use only

    bool operator == (const ValidPathInfo & i) const
    {
        return
            path == i.path
            && narHash == i.narHash
            && references == i.references;
    }
};

typedef list<ValidPathInfo> ValidPathInfos;


enum BuildMode { bmNormal, bmRepair, bmCheck, bmHash };


struct BuildResult
{
    enum Status {
        Built = 0,
        Substituted,
        AlreadyValid,
        PermanentFailure,
        InputRejected,
        OutputRejected,
        TransientFailure, // possibly transient
        CachedFailure,
        TimedOut,
        MiscFailure,
        DependencyFailed,
        LogLimitExceeded,
        NotDeterministic,
    } status = MiscFailure;
    std::string errorMsg;
    //time_t startTime = 0, stopTime = 0;
    bool success() {
        return status == Built || status == Substituted || status == AlreadyValid;
    }
};


struct BasicDerivation;
struct Derivation;
struct FSAccessor;


class Store : public std::enable_shared_from_this<Store>
{
public:

    virtual ~Store() { }

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

    /* Query the set of outgoing FS references for a store path. The
       result is not cleared. */
    virtual void queryReferences(const Path & path, PathSet & references);

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
    virtual Path addToStore(const string & name, const Path & srcPath,
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

    /* Export multiple paths in the format expected by ‘nix-store
       --import’. */
    void exportPaths(const Paths & paths, bool sign, Sink & sink);

    /* Import a sequence of NAR dumps created by exportPaths() into
       the Nix store. Optionally, the contents of the NARs are
       preloaded into the specified FS accessor to speed up subsequent
       access. */
    virtual Paths importPaths(bool requireSignature, Source & source,
        std::shared_ptr<FSAccessor> accessor) = 0;

    /* For each path, if it's a derivation, build it.  Building a
       derivation means ensuring that the output paths are valid.  If
       they are already valid, this is a no-op.  Otherwise, validity
       can be reached in two ways.  First, if the output paths is
       substitutable, then build the path that way.  Second, the
       output paths can be created by running the builder, after
       recursively building any sub-derivations. For inputs that are
       not derivations, substitute them. */
    virtual void buildPaths(const PathSet & paths, BuildMode buildMode = bmNormal) = 0;

    /* Build a single non-materialized derivation (i.e. not from an
       on-disk .drv file). Note that ‘drvPath’ is only used for
       informational purposes. */
    virtual BuildResult buildDerivation(const Path & drvPath, const BasicDerivation & drv,
        BuildMode buildMode = bmNormal) = 0;

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

    /* Register a permanent GC root. */
    Path addPermRoot(const Path & storePath,
        const Path & gcRoot, bool indirect, bool allowOutsideRootsDir = false);

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

    /* Optimise the disk space usage of the Nix store by hard-linking files
       with the same contents. */
    virtual void optimiseStore() = 0;

    /* Check the integrity of the Nix store.  Returns true if errors
       remain. */
    virtual bool verifyStore(bool checkContents, bool repair) = 0;

    /* Return an object to access files in the Nix store. */
    virtual ref<FSAccessor> getFSAccessor() = 0;

    /* Utility functions. */

    /* Read a derivation, after ensuring its existence through
       ensurePath(). */
    Derivation derivationFromPath(const Path & drvPath);

    /* Place in `paths' the set of all store paths in the file system
       closure of `storePath'; that is, all paths than can be directly
       or indirectly reached from it.  `paths' is not cleared.  If
       `flipDirection' is true, the set of paths that can reach
       `storePath' is returned; that is, the closures under the
       `referrers' relation instead of the `references' relation is
       returned. */
    void computeFSClosure(const Path & path,
        PathSet & paths, bool flipDirection = false,
        bool includeOutputs = false, bool includeDerivers = false);

    /* Given a set of paths that are to be built, return the set of
       derivations that will be built, and the set of output paths
       that will be substituted. */
    void queryMissing(const PathSet & targets,
        PathSet & willBuild, PathSet & willSubstitute, PathSet & unknown,
        unsigned long long & downloadSize, unsigned long long & narSize);

    /* Sort a set of paths topologically under the references
       relation.  If p refers to q, then p preceeds q in this list. */
    Paths topoSortPaths(const PathSet & paths);

};


class LocalFSStore : public Store
{
    ref<FSAccessor> getFSAccessor() override;
};


/* !!! These should be part of the store API, I guess. */

/* Throw an exception if `path' is not directly in the Nix store. */
void assertStorePath(const Path & path);

bool isInStore(const Path & path);
bool isStorePath(const Path & path);

/* Extract the name part of the given store path. */
string storePathToName(const Path & path);

/* Extract the hash part of the given store path. */
string storePathToHash(const Path & path);

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


/* Return a Store object to access the Nix store denoted by
   ‘uri’ (slight misnomer...). Supported values are:

   * ‘direct’: The Nix store in /nix/store and database in
     /nix/var/nix/db, accessed directly.

   * ‘daemon’: The Nix store accessed via a Unix domain socket
     connection to nix-daemon.

   * ‘file://<path>’: A binary cache stored in <path>.

   If ‘uri’ is empty, it defaults to ‘direct’ or ‘daemon’ depending on
   whether the user has write access to the local Nix store/database.
   set to true *unless* you're going to collect garbage. */
ref<Store> openStoreAt(const std::string & uri);


/* Open the store indicated by the ‘NIX_REMOTE’ environment variable. */
ref<Store> openStore();


/* Store implementation registration. */
typedef std::function<std::shared_ptr<Store>(const std::string & uri)> OpenStore;

struct RegisterStoreImplementation
{
    typedef std::vector<OpenStore> Implementations;
    static Implementations * implementations;

    RegisterStoreImplementation(OpenStore fun)
    {
        if (!implementations) implementations = new Implementations;
        implementations->push_back(fun);
    }
};



/* Display a set of paths in human-readable form (i.e., between quotes
   and separated by commas). */
string showPaths(const PathSet & paths);


ValidPathInfo decodeValidPathInfo(std::istream & str,
    bool hashGiven = false);


MakeError(SubstError, Error)
MakeError(BuildError, Error) /* denotes a permanent build failure */


}
