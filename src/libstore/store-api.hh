#pragma once

#include "path.hh"
#include "hash.hh"
#include "content-address.hh"
#include "serialise.hh"
#include "lru-cache.hh"
#include "sync.hh"
#include "globals.hh"
#include "config.hh"
#include "derivations.hh"
#include "path-info.hh"

#include <atomic>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <string>
#include <chrono>
#include <variant>


namespace nix {

/**
 * About the class hierarchy of the store implementations:
 *
 * Each store type `Foo` consists of two classes:
 *
 * 1. A class `FooConfig : virtual StoreConfig` that contains the configuration
 *   for the store
 *
 *   It should only contain members of type `const Setting<T>` (or subclasses
 *   of it) and inherit the constructors of `StoreConfig`
 *   (`using StoreConfig::StoreConfig`).
 *
 * 2. A class `Foo : virtual Store, virtual FooConfig` that contains the
 *   implementation of the store.
 *
 *   This class is expected to have a constructor `Foo(const Params & params)`
 *   that calls `StoreConfig(params)` (otherwise you're gonna encounter an
 *   `assertion failure` when trying to instantiate it).
 *
 * You can then register the new store using:
 *
 * ```
 * cpp static RegisterStoreImplementation<Foo, FooConfig> regStore;
 * ```
 */

MakeError(SubstError, Error);
MakeError(BuildError, Error); // denotes a permanent build failure
MakeError(InvalidPath, Error);
MakeError(Unsupported, Error);
MakeError(SubstituteGone, Error);
MakeError(SubstituterDisabled, Error);
MakeError(BadStorePath, Error);

MakeError(InvalidStoreURI, Error);

class FSAccessor;
class NarInfoDiskCache;
class Store;
class JSONPlaceholder;


enum CheckSigsFlag : bool { NoCheckSigs = false, CheckSigs = true };
enum SubstituteFlag : bool { NoSubstitute = false, Substitute = true };
enum AllowInvalidFlag : bool { DisallowInvalid = false, AllowInvalid = true };

/* Magic header of exportPath() output (obsolete). */
const uint32_t exportMagic = 0x4558494e;


typedef std::unordered_map<StorePath, std::unordered_set<std::string>> Roots;


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
    StorePathSet pathsToDelete;

    /* Stop after at least `maxFreed' bytes have been freed. */
    uint64_t maxFreed{std::numeric_limits<uint64_t>::max()};
};


struct GCResults
{
    /* Depending on the action, the GC roots, or the paths that would
       be or have been deleted. */
    PathSet paths;

    /* For `gcReturnDead', `gcDeleteDead' and `gcDeleteSpecific', the
       number of bytes that would be or was freed. */
    uint64_t bytesFreed = 0;
};


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

struct StoreConfig : public Config
{
    using Config::Config;

    /**
     * When constructing a store implementation, we pass in a map `params` of
     * parameters that's supposed to initialize the associated config.
     * To do that, we must use the `StoreConfig(StringMap & params)`
     * constructor, so we'd like to `delete` its default constructor to enforce
     * it.
     *
     * However, actually deleting it means that all the subclasses of
     * `StoreConfig` will have their default constructor deleted (because it's
     * supposed to call the deleted default constructor of `StoreConfig`). But
     * because we're always using virtual inheritance, the constructors of
     * child classes will never implicitely call this one, so deleting it will
     * be more painful than anything else.
     *
     * So we `assert(false)` here to ensure at runtime that the right
     * constructor is always called without having to redefine a custom
     * constructor for each `*Config` class.
     */
    StoreConfig() { assert(false); }

    virtual ~StoreConfig() { }

    virtual const std::string name() = 0;

    const PathSetting storeDir_{this, false, settings.nixStore,
        "store", "path to the Nix store"};
    const Path storeDir = storeDir_;

    const Setting<int> pathInfoCacheSize{this, 65536, "path-info-cache-size", "size of the in-memory store path information cache"};

    const Setting<bool> isTrusted{this, false, "trusted", "whether paths from this store can be used as substitutes even when they lack trusted signatures"};

    Setting<int> priority{this, 0, "priority", "priority of this substituter (lower value means higher priority)"};

    Setting<bool> wantMassQuery{this, false, "want-mass-query", "whether this substituter can be queried efficiently for path validity"};

    Setting<StringSet> systemFeatures{this, settings.systemFeatures,
        "system-features",
        "Optional features that the system this store builds on implements (like \"kvm\")."};

};

class Store : public std::enable_shared_from_this<Store>, public virtual StoreConfig
{
public:

    typedef std::map<std::string, std::string> Params;

protected:

    struct PathInfoCacheValue {

        // Time of cache entry creation or update
        std::chrono::time_point<std::chrono::steady_clock> time_point = std::chrono::steady_clock::now();

        // Null if missing
        std::shared_ptr<const ValidPathInfo> value;

        // Whether the value is valid as a cache entry. The path may not exist.
        bool isKnownNow();

        // Past tense, because a path can only be assumed to exists when
        // isKnownNow() && didExist()
        inline bool didExist() {
          return value != nullptr;
        }
    };

    struct State
    {
        // FIXME: fix key
        LRUCache<std::string, PathInfoCacheValue> pathInfoCache;
    };

    Sync<State> state;

    std::shared_ptr<NarInfoDiskCache> diskCache;

    Store(const Params & params);

public:
    /**
     * Perform any necessary effectful operation to make the store up and
     * running
     */
    virtual void init() {};

    virtual ~Store() { }

    virtual std::string getUri() = 0;

    StorePath parseStorePath(std::string_view path) const;

    std::optional<StorePath> maybeParseStorePath(std::string_view path) const;

    std::string printStorePath(const StorePath & path) const;

    // FIXME: remove
    StorePathSet parseStorePathSet(const PathSet & paths) const;

    PathSet printStorePathSet(const StorePathSet & path) const;

    /* Split a string specifying a derivation and a set of outputs
       (/nix/store/hash-foo!out1,out2,...) into the derivation path
       and the outputs. */
    StorePathWithOutputs parsePathWithOutputs(const string & s);

    /* Display a set of paths in human-readable form (i.e., between quotes
       and separated by commas). */
    std::string showPaths(const StorePathSet & paths);

    /* Return true if ‘path’ is in the Nix store (but not the Nix
       store itself). */
    bool isInStore(const Path & path) const;

    /* Return true if ‘path’ is a store path, i.e. a direct child of
       the Nix store. */
    bool isStorePath(std::string_view path) const;

    /* Split a path like /nix/store/<hash>-<name>/<bla> into
       /nix/store/<hash>-<name> and /<bla>. */
    std::pair<StorePath, Path> toStorePath(const Path & path) const;

    /* Follow symlinks until we end up with a path in the Nix store. */
    Path followLinksToStore(std::string_view path) const;

    /* Same as followLinksToStore(), but apply toStorePath() to the
       result. */
    StorePath followLinksToStorePath(std::string_view path) const;

    StorePathWithOutputs followLinksToStorePathWithOutputs(std::string_view path) const;

    /* Constructs a unique store path name. */
    StorePath makeStorePath(std::string_view type,
        std::string_view hash, std::string_view name) const;
    StorePath makeStorePath(std::string_view type,
        const Hash & hash, std::string_view name) const;

    StorePath makeOutputPath(std::string_view id,
        const Hash & hash, std::string_view name) const;

    StorePath makeFixedOutputPath(FileIngestionMethod method,
        const Hash & hash, std::string_view name,
        const StorePathSet & references = {},
        bool hasSelfReference = false) const;

    StorePath makeTextPath(std::string_view name, const Hash & hash,
        const StorePathSet & references = {}) const;

    StorePath makeFixedOutputPathFromCA(std::string_view name, ContentAddress ca,
        const StorePathSet & references = {},
        bool hasSelfReference = false) const;

    /* This is the preparatory part of addToStore(); it computes the
       store path to which srcPath is to be copied.  Returns the store
       path and the cryptographic hash of the contents of srcPath. */
    std::pair<StorePath, Hash> computeStorePathForPath(std::string_view name,
        const Path & srcPath, FileIngestionMethod method = FileIngestionMethod::Recursive,
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
    StorePath computeStorePathForText(const string & name, const string & s,
        const StorePathSet & references) const;

    /* Check whether a path is valid. */
    bool isValidPath(const StorePath & path);

protected:

    virtual bool isValidPathUncached(const StorePath & path);

public:

    /* Query which of the given paths is valid. Optionally, try to
       substitute missing paths. */
    virtual StorePathSet queryValidPaths(const StorePathSet & paths,
        SubstituteFlag maybeSubstitute = NoSubstitute);

    /* Query the set of all valid paths. Note that for some store
       backends, the name part of store paths may be replaced by 'x'
       (i.e. you'll get /nix/store/<hash>-x rather than
       /nix/store/<hash>-<name>). Use queryPathInfo() to obtain the
       full store path. FIXME: should return a set of
       std::variant<StorePath, HashPart> to get rid of this hack. */
    virtual StorePathSet queryAllValidPaths()
    { unsupported("queryAllValidPaths"); }

    constexpr static const char * MissingName = "x";

    /* Query information about a valid path. It is permitted to omit
       the name part of the store path. */
    ref<const ValidPathInfo> queryPathInfo(const StorePath & path);

    /* Asynchronous version of queryPathInfo(). */
    void queryPathInfo(const StorePath & path,
        Callback<ref<const ValidPathInfo>> callback) noexcept;

protected:

    virtual void queryPathInfoUncached(const StorePath & path,
        Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept = 0;

public:

    /* Queries the set of incoming FS references for a store path.
       The result is not cleared. */
    virtual void queryReferrers(const StorePath & path, StorePathSet & referrers)
    { unsupported("queryReferrers"); }

    /* Return all currently valid derivations that have `path' as an
       output.  (Note that the result of `queryDeriver()' is the
       derivation that was actually used to produce `path', which may
       not exist anymore.) */
    virtual StorePathSet queryValidDerivers(const StorePath & path) { return {}; };

    /* Query the outputs of the derivation denoted by `path'. */
    virtual StorePathSet queryDerivationOutputs(const StorePath & path);

    /* Query the mapping outputName => outputPath for the given derivation. All
       outputs are mentioned so ones mising the mapping are mapped to
       `std::nullopt`.  */
    virtual std::map<std::string, std::optional<StorePath>> queryPartialDerivationOutputMap(const StorePath & path)
    { unsupported("queryPartialDerivationOutputMap"); }

    /* Query the mapping outputName=>outputPath for the given derivation.
       Assume every output has a mapping and throw an exception otherwise. */
    OutputPathMap queryDerivationOutputMap(const StorePath & path);

    /* Query the full store path given the hash part of a valid store
       path, or empty if the path doesn't exist. */
    virtual std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) = 0;

    /* Query which of the given paths have substitutes. */
    virtual StorePathSet querySubstitutablePaths(const StorePathSet & paths) { return {}; };

    /* Query substitute info (i.e. references, derivers and download
       sizes) of a map of paths to their optional ca values. If a path
       does not have substitute info, it's omitted from the resulting
       ‘infos’ map. */
    virtual void querySubstitutablePathInfos(const StorePathCAMap & paths,
        SubstitutablePathInfos & infos) { return; };

    /* Import a path into the store. */
    virtual void addToStore(const ValidPathInfo & info, Source & narSource,
        RepairFlag repair = NoRepair, CheckSigsFlag checkSigs = CheckSigs) = 0;

    /* Copy the contents of a path to the store and register the
       validity the resulting path.  The resulting path is returned.
       The function object `filter' can be used to exclude files (see
       libutil/archive.hh). */
    virtual StorePath addToStore(const string & name, const Path & srcPath,
        FileIngestionMethod method = FileIngestionMethod::Recursive, HashType hashAlgo = htSHA256,
        PathFilter & filter = defaultPathFilter, RepairFlag repair = NoRepair);

    /* Copy the contents of a path to the store and register the
       validity the resulting path, using a constant amount of
       memory. */
    ValidPathInfo addToStoreSlow(std::string_view name, const Path & srcPath,
        FileIngestionMethod method = FileIngestionMethod::Recursive, HashType hashAlgo = htSHA256,
        std::optional<Hash> expectedCAHash = {});

    /* Like addToStore(), but the contents of the path are contained
       in `dump', which is either a NAR serialisation (if recursive ==
       true) or simply the contents of a regular file (if recursive ==
       false).
       `dump` may be drained */
    // FIXME: remove?
    virtual StorePath addToStoreFromDump(Source & dump, const string & name,
        FileIngestionMethod method = FileIngestionMethod::Recursive, HashType hashAlgo = htSHA256, RepairFlag repair = NoRepair)
    { unsupported("addToStoreFromDump"); }

    /* Like addToStore, but the contents written to the output path is
       a regular file containing the given string. */
    virtual StorePath addTextToStore(const string & name, const string & s,
        const StorePathSet & references, RepairFlag repair = NoRepair) = 0;

    /* Write a NAR dump of a store path. */
    virtual void narFromPath(const StorePath & path, Sink & sink) = 0;

    /* For each path, if it's a derivation, build it.  Building a
       derivation means ensuring that the output paths are valid.  If
       they are already valid, this is a no-op.  Otherwise, validity
       can be reached in two ways.  First, if the output paths is
       substitutable, then build the path that way.  Second, the
       output paths can be created by running the builder, after
       recursively building any sub-derivations. For inputs that are
       not derivations, substitute them. */
    virtual void buildPaths(
        const std::vector<StorePathWithOutputs> & paths,
        BuildMode buildMode = bmNormal);

    /* Build a single non-materialized derivation (i.e. not from an
       on-disk .drv file).

       ‘drvPath’ is used to deduplicate worker goals so it is imperative that
       is correct. That said, it doesn't literally need to be store path that
       would be calculated from writing this derivation to the store: it is OK
       if it instead is that of a Derivation which would resolve to this (by
       taking the outputs of it's input derivations and adding them as input
       sources) such that the build time referenceable-paths are the same.

       In the input-addressed case, we usually *do* use an "original"
       unresolved derivations's path, as that is what will be used in the
       `buildPaths` case. Also, the input-addressed output paths are verified
       only by that contents of that specific unresolved derivation, so it is
       nice to keep that information around so if the original derivation is
       ever obtained later, it can be verified whether the trusted user in fact
       used the proper output path.

       In the content-addressed case, we want to always use the
       resolved drv path calculated from the provided derivation. This serves
       two purposes:

         - It keeps the operation trustless, by ruling out a maliciously
           invalid drv path corresponding to a non-resolution-equivalent
           derivation.

         - For the floating case in particular, it ensures that the derivation
           to output mapping respects the resolution equivalence relation, so
           one cannot choose different resolution-equivalent derivations to
           subvert dependency coherence (i.e. the property that one doesn't end
           up with multiple different versions of dependencies without
           explicitly choosing to allow it).
    */
    virtual BuildResult buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
        BuildMode buildMode = bmNormal) = 0;

    /* Ensure that a path is valid.  If it is not currently valid, it
       may be made valid by running a substitute (if defined for the
       path). */
    virtual void ensurePath(const StorePath & path) = 0;

    /* Add a store path as a temporary root of the garbage collector.
       The root disappears as soon as we exit. */
    virtual void addTempRoot(const StorePath & path)
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
         permanent root and sees ours.

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
    string makeValidityRegistration(const StorePathSet & paths,
        bool showDerivers, bool showHash);

    /* Write a JSON representation of store path metadata, such as the
       hash and the references. If ‘includeImpureInfo’ is true,
       variable elements such as the registration time are
       included. If ‘showClosureSize’ is true, the closure size of
       each path is included. */
    void pathInfoToJSON(JSONPlaceholder & jsonOut, const StorePathSet & storePaths,
        bool includeImpureInfo, bool showClosureSize,
        Base hashBase = Base32,
        AllowInvalidFlag allowInvalid = DisallowInvalid);

    /* Return the size of the closure of the specified path, that is,
       the sum of the size of the NAR serialisation of each path in
       the closure. */
    std::pair<uint64_t, uint64_t> getClosureSize(const StorePath & storePath);

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
    virtual void addSignatures(const StorePath & storePath, const StringSet & sigs)
    { unsupported("addSignatures"); }

    /* Utility functions. */

    /* Read a derivation, after ensuring its existence through
       ensurePath(). */
    Derivation derivationFromPath(const StorePath & drvPath);

    /* Read a derivation (which must already be valid). */
    Derivation readDerivation(const StorePath & drvPath);

    /* Place in `out' the set of all store paths in the file system
       closure of `storePath'; that is, all paths than can be directly
       or indirectly reached from it.  `out' is not cleared.  If
       `flipDirection' is true, the set of paths that can reach
       `storePath' is returned; that is, the closures under the
       `referrers' relation instead of the `references' relation is
       returned. */
    virtual void computeFSClosure(const StorePathSet & paths,
        StorePathSet & out, bool flipDirection = false,
        bool includeOutputs = false, bool includeDerivers = false);

    void computeFSClosure(const StorePath & path,
        StorePathSet & out, bool flipDirection = false,
        bool includeOutputs = false, bool includeDerivers = false);

    /* Given a set of paths that are to be built, return the set of
       derivations that will be built, and the set of output paths
       that will be substituted. */
    virtual void queryMissing(const std::vector<StorePathWithOutputs> & targets,
        StorePathSet & willBuild, StorePathSet & willSubstitute, StorePathSet & unknown,
        uint64_t & downloadSize, uint64_t & narSize);

    /* Sort a set of paths topologically under the references
       relation.  If p refers to q, then p precedes q in this list. */
    StorePaths topoSortPaths(const StorePathSet & paths);

    /* Export multiple paths in the format expected by ‘nix-store
       --import’. */
    void exportPaths(const StorePathSet & paths, Sink & sink);

    void exportPath(const StorePath & path, Sink & sink);

    /* Import a sequence of NAR dumps created by exportPaths() into
       the Nix store. Optionally, the contents of the NARs are
       preloaded into the specified FS accessor to speed up subsequent
       access. */
    StorePaths importPaths(Source & source, CheckSigsFlag checkSigs = CheckSigs);

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
    virtual std::shared_ptr<std::string> getBuildLog(const StorePath & path)
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

    virtual Path toRealPath(const Path & storePath)
    {
        return storePath;
    }

    Path toRealPath(const StorePath & storePath)
    {
        return toRealPath(printStorePath(storePath));
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

struct LocalFSStoreConfig : virtual StoreConfig
{
    using StoreConfig::StoreConfig;
    // FIXME: the (StoreConfig*) cast works around a bug in gcc that causes
    // it to omit the call to the Setting constructor. Clang works fine
    // either way.
    const PathSetting rootDir{(StoreConfig*) this, true, "",
        "root", "directory prefixed to all other paths"};
    const PathSetting stateDir{(StoreConfig*) this, false,
        rootDir != "" ? rootDir + "/nix/var/nix" : settings.nixStateDir,
        "state", "directory where Nix will store state"};
    const PathSetting logDir{(StoreConfig*) this, false,
        rootDir != "" ? rootDir + "/nix/var/log/nix" : settings.nixLogDir,
        "log", "directory where Nix will store state"};
};

class LocalFSStore : public virtual Store, public virtual LocalFSStoreConfig
{
public:

    const static string drvsLogDir;

    LocalFSStore(const Params & params);

    void narFromPath(const StorePath & path, Sink & sink) override;
    ref<FSAccessor> getFSAccessor() override;

    /* Register a permanent GC root. */
    Path addPermRoot(const StorePath & storePath, const Path & gcRoot);

    virtual Path getRealStoreDir() { return storeDir; }

    Path toRealPath(const Path & storePath) override
    {
        assert(isInStore(storePath));
        return getRealStoreDir() + "/" + std::string(storePath, storeDir.size() + 1);
    }

    std::shared_ptr<std::string> getBuildLog(const StorePath & path) override;
};


/* Copy a path from one store to another. */
void copyStorePath(ref<Store> srcStore, ref<Store> dstStore,
    const StorePath & storePath, RepairFlag repair = NoRepair, CheckSigsFlag checkSigs = CheckSigs);


/* Copy store paths from one store to another. The paths may be copied
   in parallel. They are copied in a topologically sorted order (i.e.
   if A is a reference of B, then A is copied before B), but the set
   of store paths is not automatically closed; use copyClosure() for
   that. Returns a map of what each path was copied to the dstStore
   as. */
std::map<StorePath, StorePath> copyPaths(ref<Store> srcStore, ref<Store> dstStore,
    const StorePathSet & storePaths,
    RepairFlag repair = NoRepair,
    CheckSigsFlag checkSigs = CheckSigs,
    SubstituteFlag substitute = NoSubstitute);


/* Copy the closure of the specified paths from one store to another. */
void copyClosure(ref<Store> srcStore, ref<Store> dstStore,
    const StorePathSet & storePaths,
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


/* Return the default substituter stores, defined by the
   ‘substituters’ option and various legacy options. */
std::list<ref<Store>> getDefaultSubstituters();

struct StoreFactory
{
    std::set<std::string> uriSchemes;
    std::function<std::shared_ptr<Store> (const std::string & scheme, const std::string & uri, const Store::Params & params)> create;
    std::function<std::shared_ptr<StoreConfig> ()> getConfig;
};

struct Implementations
{
    static std::vector<StoreFactory> * registered;

    template<typename T, typename TConfig>
    static void add()
    {
        if (!registered) registered = new std::vector<StoreFactory>();
        StoreFactory factory{
            .uriSchemes = T::uriSchemes(),
            .create =
                ([](const std::string & scheme, const std::string & uri, const Store::Params & params)
                 -> std::shared_ptr<Store>
                 { return std::make_shared<T>(scheme, uri, params); }),
            .getConfig =
                ([]()
                 -> std::shared_ptr<StoreConfig>
                 { return std::make_shared<TConfig>(StringMap({})); })
        };
        registered->push_back(factory);
    }
};

template<typename T, typename TConfig>
struct RegisterStoreImplementation
{
    RegisterStoreImplementation()
    {
        Implementations::add<T, TConfig>();
    }
};


/* Display a set of paths in human-readable form (i.e., between quotes
   and separated by commas). */
string showPaths(const PathSet & paths);


std::optional<ValidPathInfo> decodeValidPathInfo(
    const Store & store,
    std::istream & str,
    std::optional<HashResult> hashGiven = std::nullopt);

/* Split URI into protocol+hierarchy part and its parameter set. */
std::pair<std::string, Store::Params> splitUriAndParams(const std::string & uri);

std::optional<ContentAddress> getDerivationCA(const BasicDerivation & drv);

}
