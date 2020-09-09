#pragma once

#include "sqlite.hh"

#include "pathlocks.hh"
#include "store-api.hh"
#include "sync.hh"
#include "util.hh"

#include <chrono>
#include <future>
#include <string>
#include <unordered_set>


namespace nix {


/* Nix store and database schema version.  Version 1 (or 0) was Nix <=
   0.7.  Version 2 was Nix 0.8 and 0.9.  Version 3 is Nix 0.10.
   Version 4 is Nix 0.11.  Version 5 is Nix 0.12-0.16.  Version 6 is
   Nix 1.0.  Version 7 is Nix 1.3. Version 10 is 2.0. */
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
        "require-sigs", "whether store paths should have a trusted signature on import"};

    const std::string name() override { return "Local Store"; }
};


class LocalStore : public LocalFSStore, public virtual LocalStoreConfig
{
private:

    /* Lock file used for upgrading. */
    AutoCloseFD globalLock;

    struct State
    {
        /* The SQLite database object. */
        SQLite db;

        /* Some precompiled SQLite statements. */
        SQLiteStmt stmtRegisterValidPath;
        SQLiteStmt stmtUpdatePathInfo;
        SQLiteStmt stmtAddReference;
        SQLiteStmt stmtQueryPathInfo;
        SQLiteStmt stmtQueryReferences;
        SQLiteStmt stmtQueryReferrers;
        SQLiteStmt stmtInvalidatePath;
        SQLiteStmt stmtAddDerivationOutput;
        SQLiteStmt stmtQueryValidDerivers;
        SQLiteStmt stmtQueryDerivationOutputs;
        SQLiteStmt stmtQueryPathFromHashPart;
        SQLiteStmt stmtQueryValidPaths;

        /* The file to which we write our temporary roots. */
        AutoCloseFD fdTempRoots;

        /* The last time we checked whether to do an auto-GC, or an
           auto-GC finished. */
        std::chrono::time_point<std::chrono::steady_clock> lastGCCheck;

        /* Whether auto-GC is running. If so, get gcFuture to wait for
           the GC to finish. */
        bool gcRunning = false;
        std::shared_future<void> gcFuture;

        /* How much disk space was available after the previous
           auto-GC. If the current available disk space is below
           minFree but not much below availAfterGC, then there is no
           point in starting a new GC. */
        uint64_t availAfterGC = std::numeric_limits<uint64_t>::max();

        std::unique_ptr<PublicKeys> publicKeys;
    };

    Sync<State, std::recursive_mutex> _state;

public:

    PathSetting realStoreDir_;

    const Path realStoreDir;
    const Path dbDir;
    const Path linksDir;
    const Path reservedPath;
    const Path schemaPath;
    const Path trashDir;
    const Path tempRootsDir;
    const Path fnTempRoots;

private:

    const PublicKeys & getPublicKeys();

public:

    // Hack for build-remote.cc.
    PathSet locksHeld;

    /* Initialise the local store, upgrading the schema if
       necessary. */
    LocalStore(const Params & params);

    ~LocalStore();

    /* Implementations of abstract store API methods. */

    std::string getUri() override;

    bool isValidPathUncached(StorePathOrDesc path) override;

    std::set<OwnedStorePathOrDesc> queryValidPaths(const std::set<OwnedStorePathOrDesc> & paths,
        SubstituteFlag maybeSubstitute = NoSubstitute) override;

    StorePathSet queryAllValidPaths() override;

    void queryPathInfoUncached(StorePathOrDesc,
        Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override;

    void queryReferrers(const StorePath & path, StorePathSet & referrers) override;

    StorePathSet queryValidDerivers(const StorePath & path) override;

    std::map<std::string, std::optional<StorePath>> queryPartialDerivationOutputMap(const StorePath & path) override;

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override;

    StorePathSet querySubstitutablePaths(const StorePathSet & paths) override;

    void querySubstitutablePathInfos(const StorePathSet & paths,
        const std::set<StorePathDescriptor> & caPaths,
        SubstitutablePathInfos & infos) override;

    void addToStore(const ValidPathInfo & info, Source & source,
        RepairFlag repair, CheckSigsFlag checkSigs) override;

    StorePath addToStoreFromDump(Source & dump, const string & name,
        FileIngestionMethod method, HashType hashAlgo, RepairFlag repair) override;

    StorePath addTextToStore(const string & name, const string & s,
        const StorePathSet & references, RepairFlag repair) override;

    void buildPaths(
        const std::vector<StorePathWithOutputs> & paths,
        BuildMode buildMode) override;

    BuildResult buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
        BuildMode buildMode) override;

    void ensurePath(StorePathOrDesc path) override;

    void addTempRoot(const StorePath & path) override;

    void addIndirectRoot(const Path & path) override;

    void syncWithGC() override;

private:

    typedef std::shared_ptr<AutoCloseFD> FDPtr;
    typedef list<FDPtr> FDs;

    void findTempRoots(FDs & fds, Roots & roots, bool censor);

public:

    Roots findRoots(bool censor) override;

    void collectGarbage(const GCOptions & options, GCResults & results) override;

    /* Optimise the disk space usage of the Nix store by hard-linking
       files with the same contents. */
    void optimiseStore(OptimiseStats & stats);

    void optimiseStore() override;

    /* Optimise a single store path. */
    void optimisePath(const Path & path);

    bool verifyStore(bool checkContents, RepairFlag repair) override;

    /* Register the validity of a path, i.e., that `path' exists, that
       the paths referenced by it exists, and in the case of an output
       path of a derivation, that it has been produced by a successful
       execution of the derivation (or something equivalent).  Also
       register the hash of the file system contents of the path.  The
       hash must be a SHA-256 hash. */
    void registerValidPath(const ValidPathInfo & info);

    void registerValidPaths(const ValidPathInfos & infos);

    unsigned int getProtocol() override;

    void vacuumDB();

    /* Repair the contents of the given path by redownloading it using
       a substituter (if available). */
    void repairPath(const StorePath & path);

    void addSignatures(const StorePath & storePath, const StringSet & sigs) override;

    /* If free disk space in /nix/store if below minFree, delete
       garbage until it exceeds maxFree. */
    void autoGC(bool sync = true);

private:

    int getSchema();

    void openDB(State & state, bool create);

    void makeStoreWritable();

    uint64_t queryValidPathId(State & state, const StorePath & path);

    uint64_t addValidPath(State & state, const ValidPathInfo & info, bool checkOutputs = true);

    void invalidatePath(State & state, const StorePath & path);

    /* Delete a path from the Nix store. */
    void invalidatePathChecked(const StorePath & path);

    void verifyPath(const Path & path, const StringSet & store,
        PathSet & done, StorePathSet & validPaths, RepairFlag repair, bool & errors);

    void updatePathInfo(State & state, const ValidPathInfo & info);

    void upgradeStore6();
    void upgradeStore7();
    PathSet queryValidPathsOld();
    ValidPathInfo queryPathInfoOld(const Path & path);

    struct GCState;

    void deleteGarbage(GCState & state, const Path & path);

    void tryToDelete(GCState & state, const Path & path);

    bool canReachRoot(GCState & state, StorePathSet & visited, const StorePath & path);

    void deletePathRecursive(GCState & state, const Path & path);

    bool isActiveTempFile(const GCState & state,
        const Path & path, const string & suffix);

    AutoCloseFD openGCLock(LockType lockType);

    void findRoots(const Path & path, unsigned char type, Roots & roots);

    void findRootsNoTemp(Roots & roots, bool censor);

    void findRuntimeRoots(Roots & roots, bool censor);

    void removeUnusedLinks(const GCState & state);

    Path createTempDirInStore();

    void checkDerivationOutputs(const StorePath & drvPath, const Derivation & drv);

    typedef std::unordered_set<ino_t> InodeHash;

    InodeHash loadInodeHash();
    Strings readDirectoryIgnoringInodes(const Path & path, const InodeHash & inodeHash);
    void optimisePath_(Activity * act, OptimiseStats & stats, const Path & path, InodeHash & inodeHash);

    // Internal versions that are not wrapped in retry_sqlite.
    bool isValidPath_(State & state, const StorePath & path);
    void queryReferrers(State & state, const StorePath & path, StorePathSet & referrers);

    /* Add signatures to a ValidPathInfo using the secret keys
       specified by the ‘secret-key-files’ option. */
    void signPathInfo(ValidPathInfo & info);

    /* Register the store path 'output' as the output named 'outputName' of
       derivation 'deriver'. */
    void linkDeriverToPath(const StorePath & deriver, const string & outputName, const StorePath & output);
    void linkDeriverToPath(State & state, uint64_t deriver, const string & outputName, const StorePath & output);

    Path getRealStoreDir() override { return realStoreDir; }

    void createUser(const std::string & userName, uid_t userId) override;

    friend class DerivationGoal;
    friend class SubstitutionGoal;
};


typedef std::pair<dev_t, ino_t> Inode;
typedef set<Inode> InodesSeen;


/* "Fix", or canonicalise, the meta-data of the files in a store path
   after it has been built.  In particular:
   - the last modification date on each file is set to 1 (i.e.,
     00:00:01 1/1/1970 UTC)
   - the permissions are set of 444 or 555 (i.e., read-only with or
     without execute permission; setuid bits etc. are cleared)
   - the owner and group are set to the Nix user and group, if we're
     running as root. */
void canonicalisePathMetaData(const Path & path, uid_t fromUid, InodesSeen & inodesSeen);
void canonicalisePathMetaData(const Path & path, uid_t fromUid);

void canonicaliseTimestampAndPermissions(const Path & path);

MakeError(PathInUse, Error);

}
