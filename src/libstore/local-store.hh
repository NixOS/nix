#pragma once

#include "sqlite.hh"

#include "pathlocks.hh"
#include "store-api.hh"
#include "sync.hh"
#include "util.hh"

#include <string>
#include <unordered_set>


namespace nix {


/* Nix store and database schema version.  Version 1 (or 0) was Nix <=
   0.7.  Version 2 was Nix 0.8 and 0.9.  Version 3 is Nix 0.10.
   Version 4 is Nix 0.11.  Version 5 is Nix 0.12-0.16.  Version 6 is
   Nix 1.0.  Version 7 is Nix 1.3. Version 10 is 1.12. */
const int nixSchemaVersion = 10;


extern string drvsLogDir;


struct Derivation;


struct OptimiseStats
{
    unsigned long filesLinked;
    unsigned long long bytesFreed;
    unsigned long long blocksFreed;
    OptimiseStats()
    {
        filesLinked = 0;
        bytesFreed = blocksFreed = 0;
    }
};


class LocalStore : public LocalFSStore
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
        Path fnTempRoots;
        AutoCloseFD fdTempRoots;
    };

    Sync<State, std::recursive_mutex> _state;

public:

    const Path realStoreDir;
    const Path dbDir;
    const Path linksDir;
    const Path reservedPath;
    const Path schemaPath;
    const Path trashDir;

private:

    bool requireSigs;

    PublicKeys publicKeys;

public:

    /* Initialise the local store, upgrading the schema if
       necessary. */
    LocalStore(const Params & params);

    ~LocalStore();

    /* Implementations of abstract store API methods. */

    std::string getUri() override;

    bool isValidPathUncached(const Path & path) override;

    PathSet queryValidPaths(const PathSet & paths) override;

    PathSet queryAllValidPaths() override;

    std::shared_ptr<ValidPathInfo> queryPathInfoUncached(const Path & path) override;

    void queryReferrers(const Path & path, PathSet & referrers) override;

    PathSet queryValidDerivers(const Path & path) override;

    PathSet queryDerivationOutputs(const Path & path) override;

    StringSet queryDerivationOutputNames(const Path & path) override;

    Path queryPathFromHashPart(const string & hashPart) override;

    PathSet querySubstitutablePaths(const PathSet & paths) override;

    void querySubstitutablePathInfos(const PathSet & paths,
        SubstitutablePathInfos & infos) override;

    void addToStore(const ValidPathInfo & info, const std::string & nar,
        bool repair, bool dontCheckSigs) override;

    Path addToStore(const string & name, const Path & srcPath,
        bool recursive = true, HashType hashAlgo = htSHA256,
        PathFilter & filter = defaultPathFilter, bool repair = false) override;

    /* Like addToStore(), but the contents of the path are contained
       in `dump', which is either a NAR serialisation (if recursive ==
       true) or simply the contents of a regular file (if recursive ==
       false). */
    Path addToStoreFromDump(const string & dump, const string & name,
        bool recursive = true, HashType hashAlgo = htSHA256, bool repair = false);

    Path addTextToStore(const string & name, const string & s,
        const PathSet & references, bool repair = false) override;

    void buildPaths(const PathSet & paths, BuildMode buildMode) override;

    BuildResult buildDerivation(const Path & drvPath, const BasicDerivation & drv,
        BuildMode buildMode) override;

    void ensurePath(const Path & path) override;

    void addTempRoot(const Path & path) override;

    void addIndirectRoot(const Path & path) override;

    void syncWithGC() override;

private:

    typedef std::shared_ptr<AutoCloseFD> FDPtr;
    typedef list<FDPtr> FDs;

    void readTempRoots(PathSet & tempRoots, FDs & fds);

public:

    Roots findRoots() override;

    void collectGarbage(const GCOptions & options, GCResults & results) override;

    /* Optimise the disk space usage of the Nix store by hard-linking
       files with the same contents. */
    void optimiseStore(OptimiseStats & stats);

    void optimiseStore() override;

    /* Optimise a single store path. */
    void optimisePath(const Path & path);

    bool verifyStore(bool checkContents, bool repair) override;

    /* Register the validity of a path, i.e., that `path' exists, that
       the paths referenced by it exists, and in the case of an output
       path of a derivation, that it has been produced by a successful
       execution of the derivation (or something equivalent).  Also
       register the hash of the file system contents of the path.  The
       hash must be a SHA-256 hash. */
    void registerValidPath(const ValidPathInfo & info);

    void registerValidPaths(const ValidPathInfos & infos);

    void vacuumDB();

    /* Repair the contents of the given path by redownloading it using
       a substituter (if available). */
    void repairPath(const Path & path);

    void addSignatures(const Path & storePath, const StringSet & sigs) override;

private:

    int getSchema();

    void openDB(State & state, bool create);

    void makeStoreWritable();

    uint64_t queryValidPathId(State & state, const Path & path);

    uint64_t addValidPath(State & state, const ValidPathInfo & info, bool checkOutputs = true);

    void invalidatePath(State & state, const Path & path);

    /* Delete a path from the Nix store. */
    void invalidatePathChecked(const Path & path);

    void verifyPath(const Path & path, const PathSet & store,
        PathSet & done, PathSet & validPaths, bool repair, bool & errors);

    void updatePathInfo(State & state, const ValidPathInfo & info);

    void upgradeStore6();
    void upgradeStore7();
    PathSet queryValidPathsOld();
    ValidPathInfo queryPathInfoOld(const Path & path);

    struct GCState;

    void deleteGarbage(GCState & state, const Path & path);

    void tryToDelete(GCState & state, const Path & path);

    bool canReachRoot(GCState & state, PathSet & visited, const Path & path);

    void deletePathRecursive(GCState & state, const Path & path);

    bool isActiveTempFile(const GCState & state,
        const Path & path, const string & suffix);

    int openGCLock(LockType lockType);

    void findRoots(const Path & path, unsigned char type, Roots & roots);

    void findRuntimeRoots(PathSet & roots);

    void removeUnusedLinks(const GCState & state);

    Path createTempDirInStore();

    void checkDerivationOutputs(const Path & drvPath, const Derivation & drv);

    typedef std::unordered_set<ino_t> InodeHash;

    InodeHash loadInodeHash();
    Strings readDirectoryIgnoringInodes(const Path & path, const InodeHash & inodeHash);
    void optimisePath_(OptimiseStats & stats, const Path & path, InodeHash & inodeHash);

    // Internal versions that are not wrapped in retry_sqlite.
    bool isValidPath_(State & state, const Path & path);
    void queryReferrers(State & state, const Path & path, PathSet & referrers);

    /* Add signatures to a ValidPathInfo using the secret keys
       specified by the ‘secret-key-files’ option. */
    void signPathInfo(ValidPathInfo & info);

    Path getRealStoreDir() override { return realStoreDir; }

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
