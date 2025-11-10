#pragma once
///@file

#include "nix/store/sqlite.hh"

#include "nix/store/pathlocks.hh"
#include "nix/store/store-api.hh"
#include "nix/store/indirect-root-store.hh"
#include "nix/util/sync.hh"

#include <chrono>
#include <future>
#include <string>
#include <boost/unordered/unordered_flat_set.hpp>

namespace nix {

/**
 * Nix store and database schema version.
 *
 * Version 1 (or 0) was Nix <=
 * 0.7.  Version 2 was Nix 0.8 and 0.9.  Version 3 is Nix 0.10.
 * Version 4 is Nix 0.11.  Version 5 is Nix 0.12-0.16.  Version 6 is
 * Nix 1.0.  Version 7 is Nix 1.3. Version 10 is 2.0.
 */
const int nixSchemaVersion = 10;

struct OptimiseStats
{
    unsigned long filesLinked = 0;
    uint64_t bytesFreed = 0;
};

struct LocalBuildStoreConfig : virtual LocalFSStoreConfig
{

private:
    /**
      Input for computing the build directory. See `getBuildDir()`.
     */
    Setting<std::optional<Path>> buildDir{
        this,
        std::nullopt,
        "build-dir",
        R"(
            The directory on the host, in which derivations' temporary build directories are created.

            If not set, Nix will use the `builds` subdirectory of its configured state directory.

            Note that builds are often performed by the Nix daemon, so its `build-dir` applies.

            Nix will create this directory automatically with suitable permissions if it does not exist.
            Otherwise its permissions must allow all users to traverse the directory (i.e. it must have `o+x` set, in unix parlance) for non-sandboxed builds to work correctly.

            This is also the location where [`--keep-failed`](@docroot@/command-ref/opt-common.md#opt-keep-failed) leaves its files.

            If Nix runs without sandbox, or if the platform does not support sandboxing with bind mounts (e.g. macOS), then the [`builder`](@docroot@/language/derivations.md#attr-builder)'s environment will contain this directory, instead of the virtual location [`sandbox-build-dir`](@docroot@/command-ref/conf-file.md#conf-sandbox-build-dir).

            > **Warning**
            >
            > `build-dir` must not be set to a world-writable directory.
            > Placing temporary build directories in a world-writable place allows other users to access or modify build data that is currently in use.
            > This alone is merely an impurity, but combined with another factor this has allowed malicious derivations to escape the build sandbox.
        )"};
public:
    Path getBuildDir() const;
};

struct LocalStoreConfig : std::enable_shared_from_this<LocalStoreConfig>,
                          virtual LocalFSStoreConfig,
                          virtual LocalBuildStoreConfig
{
    using LocalFSStoreConfig::LocalFSStoreConfig;

    LocalStoreConfig(std::string_view scheme, std::string_view authority, const Params & params);

private:

    /**
     * An indirection so that we don't need to refer to global settings
     * in headers.
     */
    bool getDefaultRequireSigs();

public:

    Setting<bool> requireSigs{
        this,
        getDefaultRequireSigs(),
        "require-sigs",
        "Whether store paths copied into this store should have a trusted signature."};

    Setting<bool> readOnly{
        this,
        false,
        "read-only",
        R"(
          Allow this store to be opened when its [database](@docroot@/glossary.md#gloss-nix-database) is on a read-only filesystem.

          Normally Nix attempts to open the store database in read-write mode, even for querying (when write access is not needed), causing it to fail if the database is on a read-only filesystem.

          Enable read-only mode to disable locking and open the SQLite database with the [`immutable` parameter](https://www.sqlite.org/c3ref/open.html) set.

          > **Warning**
          > Do not use this unless the filesystem is read-only.
          >
          > Using it when the filesystem is writable can cause incorrect query results or corruption errors if the database is changed by another process.
          > While the filesystem the database resides on might appear to be read-only, consider whether another user or system might have write access to it.
        )"};

    static const std::string name()
    {
        return "Local Store";
    }

    static StringSet uriSchemes()
    {
        return {"local"};
    }

    static std::string doc();

    ref<Store> openStore() const override;

    StoreReference getReference() const override;
};

class LocalStore : public virtual IndirectRootStore, public virtual GcStore
{
public:

    using Config = LocalStoreConfig;

    ref<const LocalStoreConfig> config;

private:

    /**
     * Lock file used for upgrading.
     */
    AutoCloseFD globalLock;

    struct State
    {
        /**
         * The SQLite database object.
         */
        SQLite db;

        struct Stmts;
        std::unique_ptr<Stmts> stmts;

        /**
         * The last time we checked whether to do an auto-GC, or an
         * auto-GC finished.
         */
        std::chrono::time_point<std::chrono::steady_clock> lastGCCheck;

        /**
         * Whether auto-GC is running. If so, get gcFuture to wait for
         * the GC to finish.
         */
        bool gcRunning = false;
        std::shared_future<void> gcFuture;

        /**
         * How much disk space was available after the previous
         * auto-GC. If the current available disk space is below
         * minFree but not much below availAfterGC, then there is no
         * point in starting a new GC.
         */
        uint64_t availAfterGC = std::numeric_limits<uint64_t>::max();

        std::unique_ptr<PublicKeys> publicKeys;
    };

    /**
     * Mutable state. It's behind a `ref` to reduce false sharing
     * between immutable and mutable fields.
     */
    ref<Sync<State>> _state;

public:

    const Path dbDir;
    const Path linksDir;
    const Path reservedPath;
    const Path schemaPath;
    const Path tempRootsDir;
    const Path fnTempRoots;

private:

    const PublicKeys & getPublicKeys();

public:

    /**
     * Hack for build-remote.cc.
     */
    PathSet locksHeld;

    /**
     * Initialise the local store, upgrading the schema if
     * necessary.
     */
    LocalStore(ref<const Config> params);

    ~LocalStore();

    /**
     * Implementations of abstract store API methods.
     */

    bool isValidPathUncached(const StorePath & path) override;

    StorePathSet queryValidPaths(const StorePathSet & paths, SubstituteFlag maybeSubstitute = NoSubstitute) override;

    StorePathSet queryAllValidPaths() override;

    void queryPathInfoUncached(
        const StorePath & path, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override;

    void queryReferrers(const StorePath & path, StorePathSet & referrers) override;

    StorePathSet queryValidDerivers(const StorePath & path) override;

    std::map<std::string, std::optional<StorePath>>
    queryStaticPartialDerivationOutputMap(const StorePath & path) override;

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override;

    StorePathSet querySubstitutablePaths(const StorePathSet & paths) override;

    bool pathInfoIsUntrusted(const ValidPathInfo &) override;
    bool realisationIsUntrusted(const Realisation &) override;

    void addToStore(const ValidPathInfo & info, Source & source, RepairFlag repair, CheckSigsFlag checkSigs) override;

    StorePath addToStoreFromDump(
        Source & dump,
        std::string_view name,
        FileSerialisationMethod dumpMethod,
        ContentAddressMethod hashMethod,
        HashAlgorithm hashAlgo,
        const StorePathSet & references,
        RepairFlag repair) override;

    void addTempRoot(const StorePath & path) override;

private:

    void createTempRootsFile();

    /**
     * The file to which we write our temporary roots.
     */
    Sync<AutoCloseFD> _fdTempRoots;

    /**
     * The global GC lock.
     */
    Sync<AutoCloseFD> _fdGCLock;

    /**
     * Connection to the garbage collector.
     */
    Sync<AutoCloseFD> _fdRootsSocket;

public:

    /**
     * Implementation of IndirectRootStore::addIndirectRoot().
     *
     * The weak reference merely is a symlink to `path' from
     * /nix/var/nix/gcroots/auto/<hash of `path'>.
     */
    void addIndirectRoot(const Path & path) override;

private:

    void findTempRoots(Roots & roots, bool censor);

    AutoCloseFD openGCLock();

public:

    Roots findRoots(bool censor) override;

    void collectGarbage(const GCOptions & options, GCResults & results) override;

    /**
     * Called by `collectGarbage` to trace in reverse.
     *
     * Using this rather than `queryReferrers` directly allows us to
     * fine-tune which referrers we consider for garbage collection;
     * some store implementations take advantage of this.
     */
    virtual void queryGCReferrers(const StorePath & path, StorePathSet & referrers)
    {
        return queryReferrers(path, referrers);
    }

    /**
     * Called by `collectGarbage` to recursively delete a path.
     * The default implementation simply calls `deletePath`, but it can be
     * overridden by stores that wish to provide their own deletion behaviour.
     */
    virtual void deleteStorePath(const Path & path, uint64_t & bytesFreed);

    /**
     * Optimise the disk space usage of the Nix store by hard-linking
     * files with the same contents.
     */
    void optimiseStore(OptimiseStats & stats);

    void optimiseStore() override;

    /**
     * Optimise a single store path. Optionally, test the encountered
     * symlinks for corruption.
     */
    void optimisePath(const Path & path, RepairFlag repair);

    bool verifyStore(bool checkContents, RepairFlag repair) override;

protected:

    /**
     * Result of `verifyAllValidPaths`
     */
    struct VerificationResult
    {
        /**
         * Whether any errors were encountered
         */
        bool errors;

        /**
         * A set of so-far valid paths. The store objects pointed to by
         * those paths are suitable for further validation checking.
         */
        StorePathSet validPaths;
    };

    /**
     * First, unconditional step of `verifyStore`
     */
    virtual VerificationResult verifyAllValidPaths(RepairFlag repair);

public:

    /**
     * Register the validity of a path, i.e., that `path` exists, that
     * the paths referenced by it exists, and in the case of an output
     * path of a derivation, that it has been produced by a successful
     * execution of the derivation (or something equivalent).  Also
     * register the hash of the file system contents of the path.  The
     * hash must be a SHA-256 hash.
     */
    void registerValidPath(const ValidPathInfo & info);

    virtual void registerValidPaths(const ValidPathInfos & infos);

    unsigned int getProtocol() override;

    std::optional<TrustedFlag> isTrustedClient() override;

    void vacuumDB();

    void addSignatures(const StorePath & storePath, const StringSet & sigs) override;

    /**
     * If free disk space in /nix/store if below minFree, delete
     * garbage until it exceeds maxFree.
     */
    void autoGC(bool sync = true);

    /**
     * Register the store path 'output' as the output named 'outputName' of
     * derivation 'deriver'.
     */
    void registerDrvOutput(const Realisation & info) override;
    void registerDrvOutput(const Realisation & info, CheckSigsFlag checkSigs) override;
    void cacheDrvOutputMapping(
        State & state, const uint64_t deriver, const std::string & outputName, const StorePath & output);

    std::optional<const UnkeyedRealisation> queryRealisation_(State & state, const DrvOutput & id);
    std::optional<std::pair<int64_t, UnkeyedRealisation>> queryRealisationCore_(State & state, const DrvOutput & id);
    void queryRealisationUncached(
        const DrvOutput &, Callback<std::shared_ptr<const UnkeyedRealisation>> callback) noexcept override;

    std::optional<std::string> getVersion() override;

protected:

    void verifyPath(
        const StorePath & path,
        std::function<bool(const StorePath &)> existsInStoreDir,
        StorePathSet & done,
        StorePathSet & validPaths,
        RepairFlag repair,
        bool & errors);

private:

    /**
     * Retrieve the current version of the database schema.
     * If the database does not exist yet, the version returned will be 0.
     */
    int getSchema();

    void openDB(State & state, bool create);

    void upgradeDBSchema(State & state);

    void makeStoreWritable();

    uint64_t queryValidPathId(State & state, const StorePath & path);

    uint64_t addValidPath(State & state, const ValidPathInfo & info, bool checkOutputs = true);

    void invalidatePath(State & state, const StorePath & path);

    /**
     * Delete a path from the Nix store.
     */
    void invalidatePathChecked(const StorePath & path);

    std::shared_ptr<const ValidPathInfo> queryPathInfoInternal(State & state, const StorePath & path);

    void updatePathInfo(State & state, const ValidPathInfo & info);

    PathSet queryValidPathsOld();
    ValidPathInfo queryPathInfoOld(const Path & path);

    void findRoots(const Path & path, std::filesystem::file_type type, Roots & roots);

    void findRootsNoTemp(Roots & roots, bool censor);

    void findRuntimeRoots(Roots & roots, bool censor);

    std::pair<std::filesystem::path, AutoCloseFD> createTempDirInStore();

    typedef boost::unordered_flat_set<ino_t> InodeHash;

    InodeHash loadInodeHash();
    Strings readDirectoryIgnoringInodes(const Path & path, const InodeHash & inodeHash);
    void
    optimisePath_(Activity * act, OptimiseStats & stats, const Path & path, InodeHash & inodeHash, RepairFlag repair);

    // Internal versions that are not wrapped in retry_sqlite.
    bool isValidPath_(State & state, const StorePath & path);
    void queryReferrers(State & state, const StorePath & path, StorePathSet & referrers);

    void addBuildLog(const StorePath & drvPath, std::string_view log) override;

    friend struct PathSubstitutionGoal;
    friend struct DerivationGoal;
};

} // namespace nix
