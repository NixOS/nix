#pragma once

#include "hash.hh"
#include "serialise.hh"
#include "crypto.hh"
#include "lru-cache.hh"
#include "sync.hh"
#include "globals.hh"
#include "config.hh"

#include <atomic>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <string>


namespace nix {


MakeError(SubstError, Error)
MakeError(BuildError, Error) /* denotes a permanent build failure */
MakeError(InvalidPath, Error)
MakeError(Unsupported, Error)
MakeError(SubstituteGone, Error)
MakeError(SubstituterDisabled, Error)


struct BasicDerivation;
struct Derivation;
class FSAccessor;
class NarInfoDiskCache;
class Store;
class JSONPlaceholder;


enum RepairFlag : bool { NoRepair = false, Repair = true };
enum CheckSigsFlag : bool { NoCheckSigs = false, CheckSigs = true };
enum SubstituteFlag : bool { NoSubstitute = false, Substitute = true };
enum AllowInvalidFlag : bool { DisallowInvalid = false, AllowInvalid = true };


/* Size of the hash part of store paths, in base-32 characters. */
const size_t storePathHashLen = 32; // i.e. 160 bits

/* Magic header of exportPath() output (obsolete). */
const uint32_t exportMagic = 0x4558494e;


typedef std::unordered_map<Path, std::unordered_set<std::string>> Roots;


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
    unsigned long long bytesFreed = 0;
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
    uint64_t narSize = 0; // 0 = unknown
    uint64_t id; // internal use only

    /* Whether the path is ultimately trusted, that is, it's a
       derivation output that was built locally. */
    bool ultimate = false;

    StringSet sigs; // note: not necessarily verified

    /* If non-empty, an assertion that the path is content-addressed,
       i.e., that the store path is computed from a cryptographic hash
       of the contents of the path, plus some other bits of data like
       the "name" part of the path. Such a path doesn't need
       signatures, since we don't have to trust anybody's claim that
       the path is the output of a particular derivation. (In the
       extensional store model, we have to trust that the *contents*
       of an output path of a derivation were actually produced by
       that derivation. In the intensional model, we have to trust
       that a particular output path was produced by a derivation; the
       path then implies the contents.)

       Ideally, the content-addressability assertion would just be a
       Boolean, and the store path would be computed from
       ‘storePathToName(path)’, ‘narHash’ and ‘references’. However,
       1) we've accumulated several types of content-addressed paths
       over the years; and 2) fixed-output derivations support
       multiple hash algorithms and serialisation methods (flat file
       vs NAR). Thus, ‘ca’ has one of the following forms:

       * ‘text:sha256:<sha256 hash of file contents>’: For paths
         computed by makeTextPath() / addTextToStore().

       * ‘fixed:<r?>:<ht>:<h>’: For paths computed by
         makeFixedOutputPath() / addToStore().
    */
    std::string ca;

    bool operator == (const ValidPathInfo & i) const
    {
        return
            path == i.path
            && narHash == i.narHash
            && references == i.references;
    }

    /* Return a fingerprint of the store path to be used in binary
       cache signatures. It contains the store path, the base-32
       SHA-256 hash of the NAR serialisation of the path, the size of
       the NAR, and the sorted references. The size field is strictly
       speaking superfluous, but might prevent endless/excessive data
       attacks. */
    std::string fingerprint() const;

    void sign(const SecretKey & secretKey);

    /* Return true iff the path is verifiably content-addressed. */
    bool isContentAddressed(const Store & store) const;

    static const size_t maxSigs = std::numeric_limits<size_t>::max();

    /* Return the number of signatures on this .narinfo that were
       produced by one of the specified keys, or maxSigs if the path
       is content-addressed. */
    size_t checkSignatures(const Store & store, const PublicKeys & publicKeys) const;

    /* Verify a single signature. */
    bool checkSignature(const PublicKeys & publicKeys, const std::string & sig) const;

    Strings shortRefs() const;

    virtual ~ValidPathInfo() { }
};

typedef list<ValidPathInfo> ValidPathInfos;


enum BuildMode { bmNormal, bmRepair, bmCheck };


struct BuildResult
{
    /* Note: don't remove status codes, and only add new status codes
       at the end of the list, to prevent client/server
       incompatibilities in the nix-store --serve protocol. */
    enum Status {
        Built = 0,
        Substituted,
        AlreadyValid,
        PermanentFailure,
        InputRejected,
        OutputRejected,
        TransientFailure, // possibly transient
        CachedFailure, // no longer used
        TimedOut,
        MiscFailure,
        DependencyFailed,
        LogLimitExceeded,
        NotDeterministic,
    } status = MiscFailure;
    std::string errorMsg;

    /* How many times this build was performed. */
    unsigned int timesBuilt = 0;

    /* If timesBuilt > 1, whether some builds did not produce the same
       result. (Note that 'isNonDeterministic = false' does not mean
       the build is deterministic, just that we don't have evidence of
       non-determinism.) */
    bool isNonDeterministic = false;

    /* The start/stop times of the build (or one of the rounds, if it
       was repeated). */
    time_t startTime = 0, stopTime = 0;

    bool success() {
        return status == Built || status == Substituted || status == AlreadyValid;
    }
};


class Store : public std::enable_shared_from_this<Store>, public Config
{
public:

    typedef std::map<std::string, std::string> Params;

    const PathSetting storeDir_{this, false, settings.nixStore,
        "store", "path to the Nix store"};
    const Path storeDir = storeDir_;

    const Setting<int> pathInfoCacheSize{this, 65536, "path-info-cache-size", "size of the in-memory store path information cache"};

    const Setting<bool> isTrusted{this, false, "trusted", "whether paths from this store can be used as substitutes even when they lack trusted signatures"};

protected:

    struct State
    {
        LRUCache<std::string, std::shared_ptr<ValidPathInfo>> pathInfoCache;
    };

    Sync<State> state;

    std::shared_ptr<NarInfoDiskCache> diskCache;

    Store(const Params & params);

public:

    virtual ~Store() { }

    virtual std::string getUri() = 0;

    /* Return true if ‘path’ is in the Nix store (but not the Nix
       store itself). */
    bool isInStore(const Path & path) const;

    /* Return true if ‘path’ is a store path, i.e. a direct child of
       the Nix store. */
    bool isStorePath(const Path & path) const;

    /* Throw an exception if ‘path’ is not a store path. */
    void assertStorePath(const Path & path) const;

    /* Chop off the parts after the top-level store name, e.g.,
       /nix/store/abcd-foo/bar => /nix/store/abcd-foo. */
    Path toStorePath(const Path & path) const;

    /* Follow symlinks until we end up with a path in the Nix store. */
    Path followLinksToStore(const Path & path) const;

    /* Same as followLinksToStore(), but apply toStorePath() to the
       result. */
    Path followLinksToStorePath(const Path & path) const;

    /* Constructs a unique store path name. */
    Path makeStorePath(const string & type,
        const Hash & hash, const string & name) const;

    Path makeOutputPath(const string & id,
        const Hash & hash, const string & name) const;

    Path makeFixedOutputPath(bool recursive,
        const Hash & hash, const string & name) const;

    Path makeTextPath(const string & name, const Hash & hash,
        const PathSet & references) const;

    /* This is the preparatory part of addToStore(); it computes the
       store path to which srcPath is to be copied.  Returns the store
       path and the cryptographic hash of the contents of srcPath. */
    std::pair<Path, Hash> computeStorePathForPath(const string & name,
        const Path & srcPath, bool recursive = true,
        HashType hashAlgo = htSHA256, PathFilter & filter = defaultPathFilter) const;

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
        const PathSet & references) const;

    /* Check whether a path is valid. */
    bool isValidPath(const Path & path);

protected:

    virtual bool isValidPathUncached(const Path & path);

public:

    /* Query which of the given paths is valid. Optionally, try to
       substitute missing paths. */
    virtual PathSet queryValidPaths(const PathSet & paths,
        SubstituteFlag maybeSubstitute = NoSubstitute);

    /* Query the set of all valid paths. Note that for some store
       backends, the name part of store paths may be omitted
       (i.e. you'll get /nix/store/<hash> rather than
       /nix/store/<hash>-<name>). Use queryPathInfo() to obtain the
       full store path. */
    virtual PathSet queryAllValidPaths()
    { unsupported("queryAllValidPaths"); }

    /* Query information about a valid path. It is permitted to omit
       the name part of the store path. */
    ref<const ValidPathInfo> queryPathInfo(const Path & path);

    /* Asynchronous version of queryPathInfo(). */
    void queryPathInfo(const Path & path,
        Callback<ref<ValidPathInfo>> callback) noexcept;

protected:

    virtual void queryPathInfoUncached(const Path & path,
        Callback<std::shared_ptr<ValidPathInfo>> callback) noexcept = 0;

public:

    /* Queries the set of incoming FS references for a store path.
       The result is not cleared. */
    virtual void queryReferrers(const Path & path, PathSet & referrers)
    { unsupported("queryReferrers"); }

    /* Return all currently valid derivations that have `path' as an
       output.  (Note that the result of `queryDeriver()' is the
       derivation that was actually used to produce `path', which may
       not exist anymore.) */
    virtual PathSet queryValidDerivers(const Path & path) { return {}; };

    /* Query the outputs of the derivation denoted by `path'. */
    virtual PathSet queryDerivationOutputs(const Path & path)
    { unsupported("queryDerivationOutputs"); }

    /* Query the output names of the derivation denoted by `path'. */
    virtual StringSet queryDerivationOutputNames(const Path & path)
    { unsupported("queryDerivationOutputNames"); }

    /* Query the full store path given the hash part of a valid store
       path, or "" if the path doesn't exist. */
    virtual Path queryPathFromHashPart(const string & hashPart) = 0;

    /* Query which of the given paths have substitutes. */
    virtual PathSet querySubstitutablePaths(const PathSet & paths) { return {}; };

    /* Query substitute info (i.e. references, derivers and download
       sizes) of a set of paths.  If a path does not have substitute
       info, it's omitted from the resulting ‘infos’ map. */
    virtual void querySubstitutablePathInfos(const PathSet & paths,
        SubstitutablePathInfos & infos) { return; };

    virtual bool wantMassQuery() { return false; }

    /* Import a path into the store. */
    virtual void addToStore(const ValidPathInfo & info, Source & narSource,
        RepairFlag repair = NoRepair, CheckSigsFlag checkSigs = CheckSigs,
        std::shared_ptr<FSAccessor> accessor = 0);

    // FIXME: remove
    virtual void addToStore(const ValidPathInfo & info, const ref<std::string> & nar,
        RepairFlag repair = NoRepair, CheckSigsFlag checkSigs = CheckSigs,
        std::shared_ptr<FSAccessor> accessor = 0);

    /* Copy the contents of a path to the store and register the
       validity the resulting path.  The resulting path is returned.
       The function object `filter' can be used to exclude files (see
       libutil/archive.hh). */
    virtual Path addToStore(const string & name, const Path & srcPath,
        bool recursive = true, HashType hashAlgo = htSHA256,
        PathFilter & filter = defaultPathFilter, RepairFlag repair = NoRepair) = 0;

    /* Like addToStore, but the contents written to the output path is
       a regular file containing the given string. */
    virtual Path addTextToStore(const string & name, const string & s,
        const PathSet & references, RepairFlag repair = NoRepair) = 0;

    /* Write a NAR dump of a store path. */
    virtual void narFromPath(const Path & path, Sink & sink) = 0;

    /* For each path, if it's a derivation, build it.  Building a
       derivation means ensuring that the output paths are valid.  If
       they are already valid, this is a no-op.  Otherwise, validity
       can be reached in two ways.  First, if the output paths is
       substitutable, then build the path that way.  Second, the
       output paths can be created by running the builder, after
       recursively building any sub-derivations. For inputs that are
       not derivations, substitute them. */
    virtual void buildPaths(const PathSet & paths, BuildMode buildMode = bmNormal);

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
    virtual void addTempRoot(const Path & path)
    { unsupported("addTempRoot"); }

    /* Add an indirect root, which is merely a symlink to `path' from
       /nix/var/nix/gcroots/auto/<hash of `path'>.  `path' is supposed
       to be a symlink to a store path.  The garbage collector will
       automatically remove the indirect root when it finds that
       `path' has disappeared. */
    virtual void addIndirectRoot(const Path & path)
    { unsupported("addIndirectRoot"); }

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
    virtual void syncWithGC() { };

    /* Find the roots of the garbage collector.  Each root is a pair
       (link, storepath) where `link' is the path of the symlink
       outside of the Nix store that point to `storePath'. If
       'censor' is true, privacy-sensitive information about roots
       found in /proc is censored. */
    virtual Roots findRoots(bool censor)
    { unsupported("findRoots"); }

    /* Perform a garbage collection. */
    virtual void collectGarbage(const GCOptions & options, GCResults & results)
    { unsupported("collectGarbage"); }

    /* Return a string representing information about the path that
       can be loaded into the database using `nix-store --load-db' or
       `nix-store --register-validity'. */
    string makeValidityRegistration(const PathSet & paths,
        bool showDerivers, bool showHash);

    /* Write a JSON representation of store path metadata, such as the
       hash and the references. If ‘includeImpureInfo’ is true,
       variable elements such as the registration time are
       included. If ‘showClosureSize’ is true, the closure size of
       each path is included. */
    void pathInfoToJSON(JSONPlaceholder & jsonOut, const PathSet & storePaths,
        bool includeImpureInfo, bool showClosureSize,
        AllowInvalidFlag allowInvalid = DisallowInvalid);

    /* Return the size of the closure of the specified path, that is,
       the sum of the size of the NAR serialisation of each path in
       the closure. */
    std::pair<uint64_t, uint64_t> getClosureSize(const Path & storePath);

    /* Optimise the disk space usage of the Nix store by hard-linking files
       with the same contents. */
    virtual void optimiseStore() { };

    /* Check the integrity of the Nix store.  Returns true if errors
       remain. */
    virtual bool verifyStore(bool checkContents, RepairFlag repair = NoRepair) { return false; };

    /* Return an object to access files in the Nix store. */
    virtual ref<FSAccessor> getFSAccessor()
    { unsupported("getFSAccessor"); }

    /* Add signatures to the specified store path. The signatures are
       not verified. */
    virtual void addSignatures(const Path & storePath, const StringSet & sigs)
    { unsupported("addSignatures"); }

    /* Utility functions. */

    /* Read a derivation, after ensuring its existence through
       ensurePath(). */
    Derivation derivationFromPath(const Path & drvPath);

    /* Place in `out' the set of all store paths in the file system
       closure of `storePath'; that is, all paths than can be directly
       or indirectly reached from it.  `out' is not cleared.  If
       `flipDirection' is true, the set of paths that can reach
       `storePath' is returned; that is, the closures under the
       `referrers' relation instead of the `references' relation is
       returned. */
    virtual void computeFSClosure(const PathSet & paths,
        PathSet & out, bool flipDirection = false,
        bool includeOutputs = false, bool includeDerivers = false);

    void computeFSClosure(const Path & path,
        PathSet & out, bool flipDirection = false,
        bool includeOutputs = false, bool includeDerivers = false);

    /* Given a set of paths that are to be built, return the set of
       derivations that will be built, and the set of output paths
       that will be substituted. */
    virtual void queryMissing(const PathSet & targets,
        PathSet & willBuild, PathSet & willSubstitute, PathSet & unknown,
        unsigned long long & downloadSize, unsigned long long & narSize);

    /* Sort a set of paths topologically under the references
       relation.  If p refers to q, then p precedes q in this list. */
    Paths topoSortPaths(const PathSet & paths);

    /* Export multiple paths in the format expected by ‘nix-store
       --import’. */
    void exportPaths(const Paths & paths, Sink & sink);

    void exportPath(const Path & path, Sink & sink);

    /* Import a sequence of NAR dumps created by exportPaths() into
       the Nix store. Optionally, the contents of the NARs are
       preloaded into the specified FS accessor to speed up subsequent
       access. */
    Paths importPaths(Source & source, std::shared_ptr<FSAccessor> accessor,
        CheckSigsFlag checkSigs = CheckSigs);

    struct Stats
    {
        std::atomic<uint64_t> narInfoRead{0};
        std::atomic<uint64_t> narInfoReadAverted{0};
        std::atomic<uint64_t> narInfoMissing{0};
        std::atomic<uint64_t> narInfoWrite{0};
        std::atomic<uint64_t> pathInfoCacheSize{0};
        std::atomic<uint64_t> narRead{0};
        std::atomic<uint64_t> narReadBytes{0};
        std::atomic<uint64_t> narReadCompressedBytes{0};
        std::atomic<uint64_t> narWrite{0};
        std::atomic<uint64_t> narWriteAverted{0};
        std::atomic<uint64_t> narWriteBytes{0};
        std::atomic<uint64_t> narWriteCompressedBytes{0};
        std::atomic<uint64_t> narWriteCompressionTimeMs{0};
    };

    const Stats & getStats();

    /* Return the build log of the specified store path, if available,
       or null otherwise. */
    virtual std::shared_ptr<std::string> getBuildLog(const Path & path)
    { return nullptr; }

    /* Hack to allow long-running processes like hydra-queue-runner to
       occasionally flush their path info cache. */
    void clearPathInfoCache()
    {
        state.lock()->pathInfoCache.clear();
    }

    /* Establish a connection to the store, for store types that have
       a notion of connection. Otherwise this is a no-op. */
    virtual void connect() { };

    /* Get the protocol version of this store or it's connection. */
    virtual unsigned int getProtocol()
    {
        return 0;
    };

    /* Get the priority of the store, used to order substituters. In
       particular, binary caches can specify a priority field in their
       "nix-cache-info" file. Lower value means higher priority. */
    virtual int getPriority() { return 0; }

    virtual Path toRealPath(const Path & storePath)
    {
        return storePath;
    }

    virtual void createUser(const std::string & userName, uid_t userId)
    { }

protected:

    Stats stats;

    /* Unsupported methods. */
    [[noreturn]] void unsupported(const std::string & op)
    {
        throw Unsupported("operation '%s' is not supported by store '%s'", op, getUri());
    }

};


class LocalFSStore : public virtual Store
{
public:

    // FIXME: the (Store*) cast works around a bug in gcc that causes
    // it to emit the call to the Option constructor. Clang works fine
    // either way.
    const PathSetting rootDir{(Store*) this, true, "",
        "root", "directory prefixed to all other paths"};
    const PathSetting stateDir{(Store*) this, false,
        rootDir != "" ? rootDir + "/nix/var/nix" : settings.nixStateDir,
        "state", "directory where Nix will store state"};
    const PathSetting logDir{(Store*) this, false,
        rootDir != "" ? rootDir + "/nix/var/log/nix" : settings.nixLogDir,
        "log", "directory where Nix will store state"};

    const static string drvsLogDir;

    LocalFSStore(const Params & params);

    void narFromPath(const Path & path, Sink & sink) override;
    ref<FSAccessor> getFSAccessor() override;

    /* Register a permanent GC root. */
    Path addPermRoot(const Path & storePath,
        const Path & gcRoot, bool indirect, bool allowOutsideRootsDir = false);

    virtual Path getRealStoreDir() { return storeDir; }

    Path toRealPath(const Path & storePath) override
    {
        assert(isInStore(storePath));
        return getRealStoreDir() + "/" + std::string(storePath, storeDir.size() + 1);
    }

    std::shared_ptr<std::string> getBuildLog(const Path & path) override;
};


/* Extract the name part of the given store path. */
string storePathToName(const Path & path);

/* Extract the hash part of the given store path. */
string storePathToHash(const Path & path);

/* Check whether ‘name’ is a valid store path name part, i.e. contains
   only the characters [a-zA-Z0-9\+\-\.\_\?\=] and doesn't start with
   a dot. */
void checkStoreName(const string & name);


/* Copy a path from one store to another. */
void copyStorePath(ref<Store> srcStore, ref<Store> dstStore,
    const Path & storePath, RepairFlag repair = NoRepair, CheckSigsFlag checkSigs = CheckSigs);


/* Copy store paths from one store to another. The paths may be copied
   in parallel. They are copied in a topologically sorted order
   (i.e. if A is a reference of B, then A is copied before B), but
   the set of store paths is not automatically closed; use
   copyClosure() for that. */
void copyPaths(ref<Store> srcStore, ref<Store> dstStore, const PathSet & storePaths,
    RepairFlag repair = NoRepair,
    CheckSigsFlag checkSigs = CheckSigs,
    SubstituteFlag substitute = NoSubstitute);


/* Copy the closure of the specified paths from one store to another. */
void copyClosure(ref<Store> srcStore, ref<Store> dstStore,
    const PathSet & storePaths,
    RepairFlag repair = NoRepair,
    CheckSigsFlag checkSigs = CheckSigs,
    SubstituteFlag substitute = NoSubstitute);


/* Remove the temporary roots file for this process.  Any temporary
   root becomes garbage after this point unless it has been registered
   as a (permanent) root. */
void removeTempRoots();


/* Return a Store object to access the Nix store denoted by
   ‘uri’ (slight misnomer...). Supported values are:

   * ‘local’: The Nix store in /nix/store and database in
     /nix/var/nix/db, accessed directly.

   * ‘daemon’: The Nix store accessed via a Unix domain socket
     connection to nix-daemon.

   * ‘unix://<path>’: The Nix store accessed via a Unix domain socket
     connection to nix-daemon, with the socket located at <path>.

   * ‘auto’ or ‘’: Equivalent to ‘local’ or ‘daemon’ depending on
     whether the user has write access to the local Nix
     store/database.

   * ‘file://<path>’: A binary cache stored in <path>.

   * ‘https://<path>’: A binary cache accessed via HTTP.

   * ‘s3://<path>’: A writable binary cache stored on Amazon's Simple
     Storage Service.

   * ‘ssh://[user@]<host>’: A remote Nix store accessed by running
     ‘nix-store --serve’ via SSH.

   You can pass parameters to the store implementation by appending
   ‘?key=value&key=value&...’ to the URI.
*/
ref<Store> openStore(const std::string & uri = settings.storeUri.get(),
    const Store::Params & extraParams = Store::Params());


enum StoreType {
    tDaemon,
    tLocal,
    tOther
};


StoreType getStoreType(const std::string & uri = settings.storeUri.get(),
    const std::string & stateDir = settings.nixStateDir);

/* Return the default substituter stores, defined by the
   ‘substituters’ option and various legacy options. */
std::list<ref<Store>> getDefaultSubstituters();


/* Store implementation registration. */
typedef std::function<std::shared_ptr<Store>(
    const std::string & uri, const Store::Params & params)> OpenStore;

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


/* Compute the content-addressability assertion (ValidPathInfo::ca)
   for paths created by makeFixedOutputPath() / addToStore(). */
std::string makeFixedOutputCA(bool recursive, const Hash & hash);


/* Split URI into protocol+hierarchy part and its parameter set. */
std::pair<std::string, Store::Params> splitUriAndParams(const std::string & uri);

}
