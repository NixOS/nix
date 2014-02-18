#pragma once

#include <string>

#include "store-api.hh"
#include "util.hh"
#include "pathlocks.hh"


class sqlite3;
class sqlite3_stmt;


namespace nix {


/* Nix store and database schema version.  Version 1 (or 0) was Nix <=
   0.7.  Version 2 was Nix 0.8 and 0.9.  Version 3 is Nix 0.10.
   Version 4 is Nix 0.11.  Version 5 is Nix 0.12-0.16.  Version 6 is
   Nix 1.0.  Version 7 is Nix 1.3. */
const int nixSchemaVersion = 7;


extern string drvsLogDir;


struct Derivation;


struct OptimiseStats
{
    unsigned long totalFiles;
    unsigned long sameContents;
    unsigned long filesLinked;
    unsigned long long bytesFreed;
    unsigned long long blocksFreed;
    OptimiseStats()
    {
        totalFiles = sameContents = filesLinked = 0;
        bytesFreed = blocksFreed = 0;
    }
};


struct RunningSubstituter
{
    Path program;
    Pid pid;
    AutoCloseFD to, from, error;
    FdSource fromBuf;
    bool disabled;
    RunningSubstituter() : disabled(false) { };
};


/* Wrapper object to close the SQLite database automatically. */
struct SQLite
{
    sqlite3 * db;
    SQLite() { db = 0; }
    ~SQLite();
    operator sqlite3 * () { return db; }
};


/* Wrapper object to create and destroy SQLite prepared statements. */
struct SQLiteStmt
{
    sqlite3 * db;
    sqlite3_stmt * stmt;
    unsigned int curArg;
    SQLiteStmt() { stmt = 0; }
    void create(sqlite3 * db, const string & s);
    void reset();
    ~SQLiteStmt();
    operator sqlite3_stmt * () { return stmt; }
    void bind(const string & value);
    void bind(int value);
    void bind64(long long value);
    void bind();
};


class LocalStore : public StoreAPI
{
private:
    typedef std::map<Path, RunningSubstituter> RunningSubstituters;
    RunningSubstituters runningSubstituters;

    Path linksDir;

public:

    /* Initialise the local store, upgrading the schema if
       necessary. */
    LocalStore(bool reserveSpace = true);

    ~LocalStore();

    /* Implementations of abstract store API methods. */

    bool isValidPath(const Path & path);

    PathSet queryValidPaths(const PathSet & paths);

    PathSet queryAllValidPaths();

    ValidPathInfo queryPathInfo(const Path & path);

    Hash queryPathHash(const Path & path);

    void queryReferences(const Path & path, PathSet & references);

    void queryReferrers(const Path & path, PathSet & referrers);

    Path queryDeriver(const Path & path);

    PathSet queryValidDerivers(const Path & path);

    PathSet queryDerivationOutputs(const Path & path);

    StringSet queryDerivationOutputNames(const Path & path);

    Path queryPathFromHashPart(const string & hashPart);

    PathSet querySubstitutablePaths(const PathSet & paths);

    void querySubstitutablePathInfos(const Path & substituter,
        PathSet & paths, SubstitutablePathInfos & infos);

    void querySubstitutablePathInfos(const PathSet & paths,
        SubstitutablePathInfos & infos);

    Path addToStore(const Path & srcPath,
        bool recursive = true, HashType hashAlgo = htSHA256,
        PathFilter & filter = defaultPathFilter, bool repair = false);

    /* Like addToStore(), but the contents of the path are contained
       in `dump', which is either a NAR serialisation (if recursive ==
       true) or simply the contents of a regular file (if recursive ==
       false). */
    Path addToStoreFromDump(const string & dump, const string & name,
        bool recursive = true, HashType hashAlgo = htSHA256, bool repair = false);

    Path addTextToStore(const string & name, const string & s,
        const PathSet & references, bool repair = false);

    void exportPath(const Path & path, bool sign,
        Sink & sink);

    Paths importPaths(bool requireSignature, Source & source);

    void buildPaths(const PathSet & paths, BuildMode buildMode);

    void ensurePath(const Path & path);

    void addTempRoot(const Path & path);

    void addIndirectRoot(const Path & path);

    void syncWithGC();

    Roots findRoots();

    void collectGarbage(const GCOptions & options, GCResults & results);

    /* Optimise the disk space usage of the Nix store by hard-linking
       files with the same contents. */
    void optimiseStore(OptimiseStats & stats);

    /* Optimise a single store path. */
    void optimisePath(const Path & path);

    /* Check the integrity of the Nix store.  Returns true if errors
       remain. */
    bool verifyStore(bool checkContents, bool repair);

    /* Register the validity of a path, i.e., that `path' exists, that
       the paths referenced by it exists, and in the case of an output
       path of a derivation, that it has been produced by a successful
       execution of the derivation (or something equivalent).  Also
       register the hash of the file system contents of the path.  The
       hash must be a SHA-256 hash. */
    void registerValidPath(const ValidPathInfo & info);

    void registerValidPaths(const ValidPathInfos & infos);

    /* Register that the build of a derivation with output `path' has
       failed. */
    void registerFailedPath(const Path & path);

    /* Query whether `path' previously failed to build. */
    bool hasPathFailed(const Path & path);

    PathSet queryFailedPaths();

    void clearFailedPaths(const PathSet & paths);

    void vacuumDB();

    /* Repair the contents of the given path by redownloading it using
       a substituter (if available). */
    void repairPath(const Path & path);

    /* Check whether the given valid path exists and has the right
       contents. */
    bool pathContentsGood(const Path & path);

    void markContentsGood(const Path & path);

    void setSubstituterEnv();

private:

    Path schemaPath;

    /* Lock file used for upgrading. */
    AutoCloseFD globalLock;

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
    SQLiteStmt stmtRegisterFailedPath;
    SQLiteStmt stmtHasPathFailed;
    SQLiteStmt stmtQueryFailedPaths;
    SQLiteStmt stmtClearFailedPath;
    SQLiteStmt stmtAddDerivationOutput;
    SQLiteStmt stmtQueryValidDerivers;
    SQLiteStmt stmtQueryDerivationOutputs;
    SQLiteStmt stmtQueryPathFromHashPart;

    /* Cache for pathContentsGood(). */
    std::map<Path, bool> pathContentsGoodCache;

    bool didSetSubstituterEnv;

    int getSchema();

    void openDB(bool create);

    void makeStoreWritable();

    unsigned long long queryValidPathId(const Path & path);

    unsigned long long addValidPath(const ValidPathInfo & info, bool checkOutputs = true);

    void addReference(unsigned long long referrer, unsigned long long reference);

    void appendReferrer(const Path & from, const Path & to, bool lock);

    void rewriteReferrers(const Path & path, bool purge, PathSet referrers);

    void invalidatePath(const Path & path);

    /* Delete a path from the Nix store. */
    void invalidatePathChecked(const Path & path);

    void verifyPath(const Path & path, const PathSet & store,
        PathSet & done, PathSet & validPaths, bool repair, bool & errors);

    void updatePathInfo(const ValidPathInfo & info);

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

    void removeUnusedLinks(const GCState & state);

    void startSubstituter(const Path & substituter,
        RunningSubstituter & runningSubstituter);

    string getLineFromSubstituter(RunningSubstituter & run);

    template<class T> T getIntLineFromSubstituter(RunningSubstituter & run);

    Path createTempDirInStore();

    Path importPath(bool requireSignature, Source & source);

    void checkDerivationOutputs(const Path & drvPath, const Derivation & drv);

    void optimisePath_(OptimiseStats & stats, const Path & path);

    // Internal versions that are not wrapped in retry_sqlite.
    bool isValidPath_(const Path & path);
    void queryReferrers_(const Path & path, PathSet & referrers);
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
