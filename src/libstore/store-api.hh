#pragma once
///@file

#include "path.hh"
#include "derived-path.hh"
#include "hash.hh"
#include "content-address.hh"
#include "serialise.hh"
#include "lru-cache.hh"
#include "sync.hh"
#include "globals.hh"
#include "config.hh"
#include "path-info.hh"
#include "repair-flag.hh"
#include "store-dir-config.hh"
#include "source-path.hh"

#include <nlohmann/json_fwd.hpp>
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
 * About the class hierarchy of the store types:
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
/**
 * denotes a permanent build failure
 */
MakeError(BuildError, Error);
MakeError(InvalidPath, Error);
MakeError(Unsupported, Error);
MakeError(SubstituteGone, Error);
MakeError(SubstituterDisabled, Error);

MakeError(InvalidStoreURI, Error);

struct Realisation;
struct RealisedPath;
struct DrvOutput;

struct BasicDerivation;
struct Derivation;

struct SourceAccessor;
class NarInfoDiskCache;
class Store;


typedef std::map<std::string, StorePath> OutputPathMap;


enum CheckSigsFlag : bool { NoCheckSigs = false, CheckSigs = true };
enum SubstituteFlag : bool { NoSubstitute = false, Substitute = true };

/**
 * Magic header of exportPath() output (obsolete).
 */
const uint32_t exportMagic = 0x4558494e;


enum BuildMode { bmNormal, bmRepair, bmCheck };
enum TrustedFlag : bool { NotTrusted = false, Trusted = true };

struct BuildResult;
struct KeyedBuildResult;


typedef std::map<StorePath, std::optional<ContentAddress>> StorePathCAMap;

struct StoreConfig : public StoreDirConfig
{
    typedef std::map<std::string, std::string> Params;

    using StoreDirConfig::StoreDirConfig;

    StoreConfig() = delete;

    static StringSet getDefaultSystemFeatures();

    virtual ~StoreConfig() { }

    /**
     * The name of this type of store.
     */
    virtual const std::string name() = 0;

    /**
     * Documentation for this type of store.
     */
    virtual std::string doc()
    {
        return "";
    }

    /**
     * An experimental feature this type store is gated, if it is to be
     * experimental.
     */
    virtual std::optional<ExperimentalFeature> experimentalFeature() const
    {
        return std::nullopt;
    }

    const Setting<int> pathInfoCacheSize{this, 65536, "path-info-cache-size",
        "Size of the in-memory store path metadata cache."};

    const Setting<bool> isTrusted{this, false, "trusted",
        R"(
          Whether paths from this store can be used as substitutes
          even if they are not signed by a key listed in the
          [`trusted-public-keys`](@docroot@/command-ref/conf-file.md#conf-trusted-public-keys)
          setting.
        )"};

    Setting<int> priority{this, 0, "priority",
        R"(
          Priority of this store when used as a [substituter](@docroot@/command-ref/conf-file.md#conf-substituters).
          A lower value means a higher priority.
        )"};

    Setting<bool> wantMassQuery{this, false, "want-mass-query",
        R"(
          Whether this store can be queried efficiently for path validity when used as a [substituter](@docroot@/command-ref/conf-file.md#conf-substituters).
        )"};

    Setting<StringSet> systemFeatures{this, getDefaultSystemFeatures(),
        "system-features",
        R"(
          Optional [system features](@docroot@/command-ref/conf-file.md#conf-system-features) available on the system this store uses to build derivations.

          Example: `"kvm"`
        )",
        {},
        // Don't document the machine-specific default value
        false};
};

class Store : public std::enable_shared_from_this<Store>, public virtual StoreConfig
{
protected:

    struct PathInfoCacheValue {

        /**
         * Time of cache entry creation or update
         */
        std::chrono::time_point<std::chrono::steady_clock> time_point = std::chrono::steady_clock::now();

        /**
         * Null if missing
         */
        std::shared_ptr<const ValidPathInfo> value;

        /**
         * Whether the value is valid as a cache entry. The path may not
         * exist.
         */
        bool isKnownNow();

        /**
         * Past tense, because a path can only be assumed to exists when
         * isKnownNow() && didExist()
         */
        inline bool didExist() {
          return value != nullptr;
        }
    };

    struct State
    {
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

    /**
     * Follow symlinks until we end up with a path in the Nix store.
     */
    Path followLinksToStore(std::string_view path) const;

    /**
     * Same as followLinksToStore(), but apply toStorePath() to the
     * result.
     */
    StorePath followLinksToStorePath(std::string_view path) const;

    /**
     * Check whether a path is valid.
     */
    bool isValidPath(const StorePath & path);

protected:

    virtual bool isValidPathUncached(const StorePath & path);

public:

    /**
     * If requested, substitute missing paths. This
     * implements nix-copy-closure's --use-substitutes
     * flag.
     */
    void substitutePaths(const StorePathSet & paths);

    /**
     * Query which of the given paths is valid. Optionally, try to
     * substitute missing paths.
     */
    virtual StorePathSet queryValidPaths(const StorePathSet & paths,
        SubstituteFlag maybeSubstitute = NoSubstitute);

    /**
     * Query the set of all valid paths. Note that for some store
     * backends, the name part of store paths may be replaced by 'x'
     * (i.e. you'll get /nix/store/<hash>-x rather than
     * /nix/store/<hash>-<name>). Use queryPathInfo() to obtain the
     * full store path. FIXME: should return a set of
     * std::variant<StorePath, HashPart> to get rid of this hack.
     */
    virtual StorePathSet queryAllValidPaths()
    { unsupported("queryAllValidPaths"); }

    constexpr static const char * MissingName = "x";

    /**
     * Query information about a valid path. It is permitted to omit
     * the name part of the store path.
     */
    ref<const ValidPathInfo> queryPathInfo(const StorePath & path);

    /**
     * Asynchronous version of queryPathInfo().
     */
    void queryPathInfo(const StorePath & path,
        Callback<ref<const ValidPathInfo>> callback) noexcept;

    /**
     * Version of queryPathInfo() that only queries the local narinfo cache and not
     * the actual store.
     *
     * @return `std::nullopt` if nothing is known about the path in the local narinfo cache.
     * @return `std::make_optional(nullptr)` if the path is known to not exist.
     * @return `std::make_optional(validPathInfo)` if the path is known to exist.
     */
    std::optional<std::shared_ptr<const ValidPathInfo>> queryPathInfoFromClientCache(const StorePath & path);

    /**
     * Query the information about a realisation.
     */
    std::shared_ptr<const Realisation> queryRealisation(const DrvOutput &);

    /**
     * Asynchronous version of queryRealisation().
     */
    void queryRealisation(const DrvOutput &,
        Callback<std::shared_ptr<const Realisation>> callback) noexcept;


    /**
     * Check whether the given valid path info is sufficiently attested, by
     * either being signed by a trusted public key or content-addressed, in
     * order to be included in the given store.
     *
     * These same checks would be performed in addToStore, but this allows an
     * earlier failure in the case where dependencies need to be added too, but
     * the addToStore wouldn't fail until those dependencies are added. Also,
     * we don't really want to add the dependencies listed in a nar info we
     * don't trust anyyways.
     */
    virtual bool pathInfoIsUntrusted(const ValidPathInfo &)
    {
        return true;
    }

    virtual bool realisationIsUntrusted(const Realisation & )
    {
        return true;
    }

protected:

    virtual void queryPathInfoUncached(const StorePath & path,
        Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept = 0;
    virtual void queryRealisationUncached(const DrvOutput &,
        Callback<std::shared_ptr<const Realisation>> callback) noexcept = 0;

public:

    /**
     * Queries the set of incoming FS references for a store path.
     * The result is not cleared.
     */
    virtual void queryReferrers(const StorePath & path, StorePathSet & referrers)
    { unsupported("queryReferrers"); }

    /**
     * @return all currently valid derivations that have `path` as an
     * output.
     *
     * (Note that the result of `queryDeriver()` is the derivation that
     * was actually used to produce `path`, which may not exist
     * anymore.)
     */
    virtual StorePathSet queryValidDerivers(const StorePath & path) { return {}; };

    /**
     * Query the outputs of the derivation denoted by `path`.
     */
    virtual StorePathSet queryDerivationOutputs(const StorePath & path);

    /**
     * Query the mapping outputName => outputPath for the given
     * derivation. All outputs are mentioned so ones mising the mapping
     * are mapped to `std::nullopt`.
     */
    virtual std::map<std::string, std::optional<StorePath>> queryPartialDerivationOutputMap(
        const StorePath & path,
        Store * evalStore = nullptr);

    /**
     * Like `queryPartialDerivationOutputMap` but only considers
     * statically known output paths (i.e. those that can be gotten from
     * the derivation itself.
     *
     * Just a helper function for implementing
     * `queryPartialDerivationOutputMap`.
     */
    virtual std::map<std::string, std::optional<StorePath>> queryStaticPartialDerivationOutputMap(
        const StorePath & path);

    /**
     * Query the mapping outputName=>outputPath for the given derivation.
     * Assume every output has a mapping and throw an exception otherwise.
     */
    OutputPathMap queryDerivationOutputMap(const StorePath & path, Store * evalStore = nullptr);

    /**
     * Query the full store path given the hash part of a valid store
     * path, or empty if the path doesn't exist.
     */
    virtual std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) = 0;

    /**
     * Query which of the given paths have substitutes.
     */
    virtual StorePathSet querySubstitutablePaths(const StorePathSet & paths) { return {}; };

    /**
     * Query substitute info (i.e. references, derivers and download
     * sizes) of a map of paths to their optional ca values. The info of
     * the first succeeding substituter for each path will be returned.
     * If a path does not have substitute info, it's omitted from the
     * resulting ‘infos’ map.
     */
    virtual void querySubstitutablePathInfos(const StorePathCAMap & paths,
        SubstitutablePathInfos & infos);

    /**
     * Import a path into the store.
     */
    virtual void addToStore(const ValidPathInfo & info, Source & narSource,
        RepairFlag repair = NoRepair, CheckSigsFlag checkSigs = CheckSigs) = 0;

    /**
     * A list of paths infos along with a source providing the content
     * of the associated store path
     */
    using PathsSource = std::vector<std::pair<ValidPathInfo, std::unique_ptr<Source>>>;

    /**
     * Import multiple paths into the store.
     */
    virtual void addMultipleToStore(
        Source & source,
        RepairFlag repair = NoRepair,
        CheckSigsFlag checkSigs = CheckSigs);

    virtual void addMultipleToStore(
        PathsSource & pathsToCopy,
        Activity & act,
        RepairFlag repair = NoRepair,
        CheckSigsFlag checkSigs = CheckSigs);

    /**
     * Copy the contents of a path to the store and register the
     * validity the resulting path.
     *
     * @return The resulting path is returned.
     * @param filter This function can be used to exclude files (see
     * libutil/archive.hh).
     */
    virtual StorePath addToStore(
        std::string_view name,
        SourceAccessor & accessor,
        const CanonPath & path,
        ContentAddressMethod method = FileIngestionMethod::Recursive,
        HashAlgorithm hashAlgo = HashAlgorithm::SHA256,
        const StorePathSet & references = StorePathSet(),
        PathFilter & filter = defaultPathFilter,
        RepairFlag repair = NoRepair);

    /**
     * Copy the contents of a path to the store and register the
     * validity the resulting path, using a constant amount of
     * memory.
     */
    ValidPathInfo addToStoreSlow(
        std::string_view name,
        SourceAccessor & accessor,
        const CanonPath & path,
        ContentAddressMethod method = FileIngestionMethod::Recursive,
        HashAlgorithm hashAlgo = HashAlgorithm::SHA256,
        const StorePathSet & references = StorePathSet(),
        std::optional<Hash> expectedCAHash = {});

    /**
     * Like addToStore(), but the contents of the path are contained
     * in `dump`, which is either a NAR serialisation (if recursive ==
     * true) or simply the contents of a regular file (if recursive ==
     * false).
     *
     * `dump` may be drained.
     *
     * @param dumpMethod What serialisation format is `dump`, i.e. how
     * to deserialize it. Must either match hashMethod or be
     * `FileSerialisationMethod::Recursive`.
     *
     * @param hashMethod How content addressing? Need not match be the
     * same as `dumpMethod`.
     *
     * @todo remove?
     */
    virtual StorePath addToStoreFromDump(
        Source & dump,
        std::string_view name,
        FileSerialisationMethod dumpMethod = FileSerialisationMethod::Recursive,
        ContentAddressMethod hashMethod = FileIngestionMethod::Recursive,
        HashAlgorithm hashAlgo = HashAlgorithm::SHA256,
        const StorePathSet & references = StorePathSet(),
        RepairFlag repair = NoRepair) = 0;

    /**
     * Add a mapping indicating that `deriver!outputName` maps to the output path
     * `output`.
     *
     * This is redundant for known-input-addressed and fixed-output derivations
     * as this information is already present in the drv file, but necessary for
     * floating-ca derivations and their dependencies as there's no way to
     * retrieve this information otherwise.
     */
    virtual void registerDrvOutput(const Realisation & output)
    { unsupported("registerDrvOutput"); }
    virtual void registerDrvOutput(const Realisation & output, CheckSigsFlag checkSigs)
    { return registerDrvOutput(output); }

    /**
     * Write a NAR dump of a store path.
     */
    virtual void narFromPath(const StorePath & path, Sink & sink) = 0;

    /**
     * For each path, if it's a derivation, build it.  Building a
     * derivation means ensuring that the output paths are valid.  If
     * they are already valid, this is a no-op.  Otherwise, validity
     * can be reached in two ways.  First, if the output paths is
     * substitutable, then build the path that way.  Second, the
     * output paths can be created by running the builder, after
     * recursively building any sub-derivations. For inputs that are
     * not derivations, substitute them.
     */
    virtual void buildPaths(
        const std::vector<DerivedPath> & paths,
        BuildMode buildMode = bmNormal,
        std::shared_ptr<Store> evalStore = nullptr);

    /**
     * Like buildPaths(), but return a vector of \ref BuildResult
     * BuildResults corresponding to each element in paths. Note that in
     * case of a build/substitution error, this function won't throw an
     * exception, but return a BuildResult containing an error message.
     */
    virtual std::vector<KeyedBuildResult> buildPathsWithResults(
        const std::vector<DerivedPath> & paths,
        BuildMode buildMode = bmNormal,
        std::shared_ptr<Store> evalStore = nullptr);

    /**
     * Build a single non-materialized derivation (i.e. not from an
     * on-disk .drv file).
     *
     * @param drvPath This is used to deduplicate worker goals so it is
     * imperative that is correct. That said, it doesn't literally need
     * to be store path that would be calculated from writing this
     * derivation to the store: it is OK if it instead is that of a
     * Derivation which would resolve to this (by taking the outputs of
     * it's input derivations and adding them as input sources) such
     * that the build time referenceable-paths are the same.
     *
     * In the input-addressed case, we usually *do* use an "original"
     * unresolved derivations's path, as that is what will be used in the
     * buildPaths case. Also, the input-addressed output paths are verified
     * only by that contents of that specific unresolved derivation, so it is
     * nice to keep that information around so if the original derivation is
     * ever obtained later, it can be verified whether the trusted user in fact
     * used the proper output path.
     *
     * In the content-addressed case, we want to always use the resolved
     * drv path calculated from the provided derivation. This serves two
     * purposes:
     *
     *   - It keeps the operation trustless, by ruling out a maliciously
     *     invalid drv path corresponding to a non-resolution-equivalent
     *     derivation.
     *
     *   - For the floating case in particular, it ensures that the derivation
     *     to output mapping respects the resolution equivalence relation, so
     *     one cannot choose different resolution-equivalent derivations to
     *     subvert dependency coherence (i.e. the property that one doesn't end
     *     up with multiple different versions of dependencies without
     *     explicitly choosing to allow it).
     */
    virtual BuildResult buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
        BuildMode buildMode = bmNormal);

    /**
     * Ensure that a path is valid.  If it is not currently valid, it
     * may be made valid by running a substitute (if defined for the
     * path).
     */
    virtual void ensurePath(const StorePath & path);

    /**
     * Add a store path as a temporary root of the garbage collector.
     * The root disappears as soon as we exit.
     */
    virtual void addTempRoot(const StorePath & path)
    { debug("not creating temporary root, store doesn't support GC"); }

    /**
     * @return a string representing information about the path that
     * can be loaded into the database using `nix-store --load-db` or
     * `nix-store --register-validity`.
     */
    std::string makeValidityRegistration(const StorePathSet & paths,
        bool showDerivers, bool showHash);

    /**
     * Optimise the disk space usage of the Nix store by hard-linking files
     * with the same contents.
     */
    virtual void optimiseStore() { };

    /**
     * Check the integrity of the Nix store.
     *
     * @return true if errors remain.
     */
    virtual bool verifyStore(bool checkContents, RepairFlag repair = NoRepair) { return false; };

    /**
     * @return An object to access files in the Nix store.
     */
    virtual ref<SourceAccessor> getFSAccessor(bool requireValidPath = true) = 0;

    /**
     * Repair the contents of the given path by redownloading it using
     * a substituter (if available).
     */
    virtual void repairPath(const StorePath & path);

    /**
     * Add signatures to the specified store path. The signatures are
     * not verified.
     */
    virtual void addSignatures(const StorePath & storePath, const StringSet & sigs)
    { unsupported("addSignatures"); }

    /* Utility functions. */

    /**
     * Read a derivation, after ensuring its existence through
     * ensurePath().
     */
    Derivation derivationFromPath(const StorePath & drvPath);

    /**
     * Read a derivation (which must already be valid).
     */
    Derivation readDerivation(const StorePath & drvPath);

    /**
     * Read a derivation from a potentially invalid path.
     */
    Derivation readInvalidDerivation(const StorePath & drvPath);

    /**
     * @param [out] out Place in here the set of all store paths in the
     * file system closure of `storePath`; that is, all paths than can
     * be directly or indirectly reached from it. `out` is not cleared.
     *
     * @param flipDirection If true, the set of paths that can reach
     * `storePath` is returned; that is, the closures under the
     * `referrers` relation instead of the `references` relation is
     * returned.
     */
    virtual void computeFSClosure(const StorePathSet & paths,
        StorePathSet & out, bool flipDirection = false,
        bool includeOutputs = false, bool includeDerivers = false);

    void computeFSClosure(const StorePath & path,
        StorePathSet & out, bool flipDirection = false,
        bool includeOutputs = false, bool includeDerivers = false);

    /**
     * Given a set of paths that are to be built, return the set of
     * derivations that will be built, and the set of output paths that
     * will be substituted.
     */
    virtual void queryMissing(const std::vector<DerivedPath> & targets,
        StorePathSet & willBuild, StorePathSet & willSubstitute, StorePathSet & unknown,
        uint64_t & downloadSize, uint64_t & narSize);

    /**
     * Sort a set of paths topologically under the references
     * relation.  If p refers to q, then p precedes q in this list.
     */
    StorePaths topoSortPaths(const StorePathSet & paths);

    /**
     * Export multiple paths in the format expected by ‘nix-store
     * --import’.
     */
    void exportPaths(const StorePathSet & paths, Sink & sink);

    void exportPath(const StorePath & path, Sink & sink);

    /**
     * Import a sequence of NAR dumps created by exportPaths() into the
     * Nix store. Optionally, the contents of the NARs are preloaded
     * into the specified FS accessor to speed up subsequent access.
     */
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

    /**
     * Computes the full closure of of a set of store-paths for e.g.
     * derivations that need this information for `exportReferencesGraph`.
     */
    StorePathSet exportReferences(const StorePathSet & storePaths, const StorePathSet & inputPaths);

    /**
     * Given a store path, return the realisation actually used in the realisation of this path:
     * - If the path is a content-addressed derivation, try to resolve it
     * - Otherwise, find one of its derivers
     */
    std::optional<StorePath> getBuildDerivationPath(const StorePath &);

    /**
     * Hack to allow long-running processes like hydra-queue-runner to
     * occasionally flush their path info cache.
     */
    void clearPathInfoCache()
    {
        state.lock()->pathInfoCache.clear();
    }

    /**
     * Establish a connection to the store, for store types that have
     * a notion of connection. Otherwise this is a no-op.
     */
    virtual void connect() { };

    /**
     * Get the protocol version of this store or it's connection.
     */
    virtual unsigned int getProtocol()
    {
        return 0;
    };

    /**
     * @return/ whether store trusts *us*.
     *
     * `std::nullopt` means we do not know.
     *
     * @note This is the opposite of the StoreConfig::isTrusted
     * store setting. That is about whether *we* trust the store.
     */
    virtual std::optional<TrustedFlag> isTrustedClient() = 0;


    virtual Path toRealPath(const Path & storePath)
    {
        return storePath;
    }

    Path toRealPath(const StorePath & storePath)
    {
        return toRealPath(printStorePath(storePath));
    }

    /**
     * Synchronises the options of the client with those of the daemon
     * (a no-op when there’s no daemon)
     */
    virtual void setOptions() { }

    virtual std::optional<std::string> getVersion() { return {}; }

protected:

    Stats stats;

    /**
     * Helper for methods that are not unsupported: this is used for
     * default definitions for virtual methods that are meant to be overriden.
     *
     * @todo Using this should be a last resort. It is better to make
     * the method "virtual pure" and/or move it to a subclass.
     */
    [[noreturn]] void unsupported(const std::string & op)
    {
        throw Unsupported("operation '%s' is not supported by store '%s'", op, getUri());
    }

};


/**
 * Copy a path from one store to another.
 */
void copyStorePath(
    Store & srcStore,
    Store & dstStore,
    const StorePath & storePath,
    RepairFlag repair = NoRepair,
    CheckSigsFlag checkSigs = CheckSigs);


/**
 * Copy store paths from one store to another. The paths may be copied
 * in parallel. They are copied in a topologically sorted order (i.e. if
 * A is a reference of B, then A is copied before B), but the set of
 * store paths is not automatically closed; use copyClosure() for that.
 *
 * @return a map of what each path was copied to the dstStore as.
 */
std::map<StorePath, StorePath> copyPaths(
    Store & srcStore, Store & dstStore,
    const std::set<RealisedPath> &,
    RepairFlag repair = NoRepair,
    CheckSigsFlag checkSigs = CheckSigs,
    SubstituteFlag substitute = NoSubstitute);

std::map<StorePath, StorePath> copyPaths(
    Store & srcStore, Store & dstStore,
    const StorePathSet & paths,
    RepairFlag repair = NoRepair,
    CheckSigsFlag checkSigs = CheckSigs,
    SubstituteFlag substitute = NoSubstitute);

/**
 * Copy the closure of `paths` from `srcStore` to `dstStore`.
 */
void copyClosure(
    Store & srcStore, Store & dstStore,
    const std::set<RealisedPath> & paths,
    RepairFlag repair = NoRepair,
    CheckSigsFlag checkSigs = CheckSigs,
    SubstituteFlag substitute = NoSubstitute);

void copyClosure(
    Store & srcStore, Store & dstStore,
    const StorePathSet & paths,
    RepairFlag repair = NoRepair,
    CheckSigsFlag checkSigs = CheckSigs,
    SubstituteFlag substitute = NoSubstitute);

/**
 * Remove the temporary roots file for this process.  Any temporary
 * root becomes garbage after this point unless it has been registered
 * as a (permanent) root.
 */
void removeTempRoots();


/**
 * Resolve the derived path completely, failing if any derivation output
 * is unknown.
 */
StorePath resolveDerivedPath(Store &, const SingleDerivedPath &, Store * evalStore = nullptr);
OutputPathMap resolveDerivedPath(Store &, const DerivedPath::Built &, Store * evalStore = nullptr);


/**
 * @return a Store object to access the Nix store denoted by
 * ‘uri’ (slight misnomer...).
 *
 * @param uri Supported values are:
 *
 * - ‘local’: The Nix store in /nix/store and database in
 *   /nix/var/nix/db, accessed directly.
 *
 * - ‘daemon’: The Nix store accessed via a Unix domain socket
 *   connection to nix-daemon.
 *
 * - ‘unix://<path>’: The Nix store accessed via a Unix domain socket
 *   connection to nix-daemon, with the socket located at <path>.
 *
 * - ‘auto’ or ‘’: Equivalent to ‘local’ or ‘daemon’ depending on
 *   whether the user has write access to the local Nix
 *   store/database.
 *
 * - ‘file://<path>’: A binary cache stored in <path>.
 *
 * - ‘https://<path>’: A binary cache accessed via HTTP.
 *
 * - ‘s3://<path>’: A writable binary cache stored on Amazon's Simple
 *   Storage Service.
 *
 * - ‘ssh://[user@]<host>’: A remote Nix store accessed by running
 *   ‘nix-store --serve’ via SSH.
 *
 * You can pass parameters to the store type by appending
 * ‘?key=value&key=value&...’ to the URI.
 */
ref<Store> openStore(const std::string & uri = settings.storeUri.get(),
    const Store::Params & extraParams = Store::Params());


/**
 * @return the default substituter stores, defined by the
 * ‘substituters’ option and various legacy options.
 */
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


/**
 * Display a set of paths in human-readable form (i.e., between quotes
 * and separated by commas).
 */
std::string showPaths(const PathSet & paths);


std::optional<ValidPathInfo> decodeValidPathInfo(
    const Store & store,
    std::istream & str,
    std::optional<HashResult> hashGiven = std::nullopt);

/**
 * Split URI into protocol+hierarchy part and its parameter set.
 */
std::pair<std::string, Store::Params> splitUriAndParams(const std::string & uri);

const ContentAddress * getDerivationCA(const BasicDerivation & drv);

std::map<DrvOutput, StorePath> drvOutputReferences(
    Store & store,
    const Derivation & drv,
    const StorePath & outputPath,
    Store * evalStore = nullptr);

}
