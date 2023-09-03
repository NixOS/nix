#pragma once
///@file

#include "sqlite.hh"

#include "pathlocks.hh"
#include "store-api.hh"
#include "indirect-root-store.hh"
#include "sync.hh"
#include "util.hh"

#include <chrono>
#include <future>
#include <string>
#include <unordered_set>


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
    uint64_t blocksFreed = 0;
};

struct LocalStoreConfig : virtual LocalFSStoreConfig
{
    using LocalFSStoreConfig::LocalFSStoreConfig;

    Setting<bool> requireSigs{(StoreConfig*) this,
        settings.requireSigs,
        "require-sigs",
        "Whether store paths copied into this store should have a trusted signature."};

    Setting<bool> readOnly{(StoreConfig*) this,
        false,
        "read-only",
        R"(
          Allow this store to be opened when its [database](@docroot@/glossary.md#gloss-nix-database) is on a read-only filesystem.

          Normally Nix will attempt to open the store database in read-write mode, even for querying (when write access is not needed), causing it to fail if the database is on a read-only filesystem.

          Enable read-only mode to disable locking and open the SQLite database with the [`immutable` parameter](https://www.sqlite.org/c3ref/open.html) set.

          > **Warning**
          > Do not use this unless the filesystem is read-only.
          >
          > Using it when the filesystem is writable can cause incorrect query results or corruption errors if the database is changed by another process.
          > While the filesystem the database resides on might appear to be read-only, consider whether another user or system might have write access to it.
        )"};

    const std::string name() override { return "Local Store"; }

    std::string doc() override;
};

class LocalStore : public virtual LocalStoreConfig
    , public virtual IndirectRootStore
    , public virtual GcStore
{
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

    Sync<State> _state;

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
    LocalStore(const Params & params);
    LocalStore(std::string scheme, std::string path, const Params & params);

    ~LocalStore();

    static std::set<std::string> uriSchemes()
    { return {}; }

    /**
     * Implementations of abstract store API methods.
     */

    std::string getUri() override;

    bool isValidPathUncached(const StorePath & path) override;

    StorePathSet queryValidPaths(const StorePathSet & paths,
        SubstituteFlag maybeSubstitute = NoSubstitute) override;

    StorePathSet queryAllValidPaths() override;

    void queryPathInfoUncached(const StorePath & path,
        Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override;

    void queryReferrers(const StorePath & path, StorePathSet & referrers) override;

    StorePathSet queryValidDerivers(const StorePath & path) override;

    std::map<std::string, std::optional<StorePath>> queryStaticPartialDerivationOutputMap(const StorePath & path) override;

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override;

    StorePathSet querySubstitutablePaths(const StorePathSet & paths) override;

    bool pathInfoIsUntrusted(const ValidPathInfo &) override;
    bool realisationIsUntrusted(const Realisation & ) override;

    void addToStore(const ValidPathInfo & info, Source & source,
        RepairFlag repair, CheckSigsFlag checkSigs) override;

    StorePath addToStoreFromDump(Source & dump, std::string_view name,
        FileIngestionMethod method, HashType hashAlgo, RepairFlag repair, const StorePathSet & references) override;

    StorePath addTextToStore(
        std::string_view name,
        std::string_view s,
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

    /**
     * Register the validity of a path, i.e., that `path` exists, that
     * the paths referenced by it exists, and in the case of an output
     * path of a derivation, that it has been produced by a successful
     * execution of the derivation (or something equivalent).  Also
     * register the hash of the file system contents of the path.  The
     * hash must be a SHA-256 hash.
     */
    void registerValidPath(const ValidPathInfo & info);

    void registerValidPaths(const ValidPathInfos & infos);

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
        State & state,
        const uint64_t deriver,
        const std::string & outputName,
        const StorePath & output);

    std::optional<const Realisation> queryRealisation_(State & state, const DrvOutput & id);
    std::optional<std::pair<int64_t, Realisation>> queryRealisationCore_(State & state, const DrvOutput & id);
    void queryRealisationUncached(const DrvOutput&,
        Callback<std::shared_ptr<const Realisation>> callback) noexcept override;

    std::optional<std::string> getVersion() override;

private:

    /**
     * Retrieve the current version of the database schema.
     * If the database does not exist yet, the version returned will be 0.
     */
    int getSchema();

    void openDB(State & state, bool create);

    void makeStoreWritable();

    uint64_t queryValidPathId(State & state, const StorePath & path);

    uint64_t addValidPath(State & state, const ValidPathInfo & info, bool checkOutputs = true);

    void invalidatePath(State & state, const StorePath & path);

    /**
     * Delete a path from the Nix store.
     */
    void invalidatePathChecked(const StorePath & path);

    void verifyPath(const StorePath & path, const StorePathSet & store,
        StorePathSet & done, StorePathSet & validPaths, RepairFlag repair, bool & errors);

    std::shared_ptr<const ValidPathInfo> queryPathInfoInternal(State & state, const StorePath & path);

    void updatePathInfo(State & state, const ValidPathInfo & info);

    void upgradeStore6();
    void upgradeStore7();
    PathSet queryValidPathsOld();
    ValidPathInfo queryPathInfoOld(const Path & path);

    void findRoots(const Path & path, unsigned char type, Roots & roots);

    void findRootsNoTemp(Roots & roots, bool censor);

    void findRuntimeRoots(Roots & roots, bool censor);

    std::pair<Path, AutoCloseFD> createTempDirInStore();

    typedef std::unordered_set<ino_t> InodeHash;

    InodeHash loadInodeHash();
    Strings readDirectoryIgnoringInodes(const Path & path, const InodeHash & inodeHash);
    void optimisePath_(Activity * act, OptimiseStats & stats, const Path & path, InodeHash & inodeHash, RepairFlag repair);

    // Internal versions that are not wrapped in retry_sqlite.
    bool isValidPath_(State & state, const StorePath & path);
    void queryReferrers(State & state, const StorePath & path, StorePathSet & referrers);

    /**
     * Add signatures to a ValidPathInfo or Realisation using the secret keys
     * specified by the ‘secret-key-files’ option.
     */
    void signPathInfo(ValidPathInfo & info);
    void signRealisation(Realisation &);

    // XXX: Make a generic `Store` method
    ContentAddress hashCAPath(
        const ContentAddressMethod & method,
        const HashType & hashType,
        const StorePath & path);

    ContentAddress hashCAPath(
        const ContentAddressMethod & method,
        const HashType & hashType,
        const Path & path,
        const std::string_view pathHash
    );

    void addBuildLog(const StorePath & drvPath, std::string_view log) override;

    friend struct LocalDerivationGoal;
    friend struct PathSubstitutionGoal;
    friend struct SubstitutionGoal;
    friend struct DerivationGoal;
};


typedef std::pair<dev_t, ino_t> Inode;
typedef std::set<Inode> InodesSeen;


/**
 * "Fix", or canonicalise, the meta-data of the files in a store path
 * after it has been built.  In particular:
 *
 * - the last modification date on each file is set to 1 (i.e.,
 *   00:00:01 1/1/1970 UTC)
 *
 * - the permissions are set of 444 or 555 (i.e., read-only with or
 *   without execute permission; setuid bits etc. are cleared)
 *
 * - the owner and group are set to the Nix user and group, if we're
 *   running as root.
 *
 * If uidRange is not empty, this function will throw an error if it
 * encounters files owned by a user outside of the closed interval
 * [uidRange->first, uidRange->second].
 */
void canonicalisePathMetaData(
    const Path & path,
    std::optional<std::pair<uid_t, uid_t>> uidRange,
    InodesSeen & inodesSeen);
void canonicalisePathMetaData(
    const Path & path,
    std::optional<std::pair<uid_t, uid_t>> uidRange);

void canonicaliseTimestampAndPermissions(const Path & path);

MakeError(PathInUse, Error);

}
