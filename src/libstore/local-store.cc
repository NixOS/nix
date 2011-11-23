#include "config.h"
#include "local-store.hh"
#include "globals.hh"
#include "archive.hh"
#include "pathlocks.hh"
#include "worker-protocol.hh"
#include "derivations.hh"
    
#include <iostream>
#include <algorithm>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

#include <sqlite3.h>


namespace nix {


MakeError(SQLiteError, Error);
MakeError(SQLiteBusy, SQLiteError);


static void throwSQLiteError(sqlite3 * db, const format & f)
    __attribute__ ((noreturn));

static void throwSQLiteError(sqlite3 * db, const format & f)
{
    int err = sqlite3_errcode(db);
    if (err == SQLITE_BUSY) {
        printMsg(lvlError, "warning: SQLite database is busy");
        /* Sleep for a while since retrying the transaction right away
           is likely to fail again. */
#if HAVE_NANOSLEEP
        struct timespec t;
        t.tv_sec = 0;
        t.tv_nsec = 100 * 1000 * 1000; /* 0.1s */
        nanosleep(&t, 0);
#else
        sleep(1);
#endif
        throw SQLiteBusy(format("%1%: %2%") % f.str() % sqlite3_errmsg(db));
    }
    else
        throw SQLiteError(format("%1%: %2%") % f.str() % sqlite3_errmsg(db));
}


SQLite::~SQLite()
{
    try {
        if (db && sqlite3_close(db) != SQLITE_OK)
            throwSQLiteError(db, "closing database");
    } catch (...) {
        ignoreException();
    }
}


void SQLiteStmt::create(sqlite3 * db, const string & s)
{
    checkInterrupt();
    assert(!stmt);
    if (sqlite3_prepare_v2(db, s.c_str(), -1, &stmt, 0) != SQLITE_OK)
        throwSQLiteError(db, "creating statement");
    this->db = db;
}


void SQLiteStmt::reset()
{
    assert(stmt);
    /* Note: sqlite3_reset() returns the error code for the most
       recent call to sqlite3_step().  So ignore it. */
    sqlite3_reset(stmt);
    curArg = 1;
}


SQLiteStmt::~SQLiteStmt()
{
    try {
        if (stmt && sqlite3_finalize(stmt) != SQLITE_OK)
            throwSQLiteError(db, "finalizing statement");
    } catch (...) {
        ignoreException();
    }
}


void SQLiteStmt::bind(const string & value)
{
    if (sqlite3_bind_text(stmt, curArg++, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
        throwSQLiteError(db, "binding argument");
}


void SQLiteStmt::bind(int value)
{
    if (sqlite3_bind_int(stmt, curArg++, value) != SQLITE_OK)
        throwSQLiteError(db, "binding argument");
}


void SQLiteStmt::bind64(long long value)
{
    if (sqlite3_bind_int64(stmt, curArg++, value) != SQLITE_OK)
        throwSQLiteError(db, "binding argument");
}


void SQLiteStmt::bind()
{
    if (sqlite3_bind_null(stmt, curArg++) != SQLITE_OK)
        throwSQLiteError(db, "binding argument");
}


/* Helper class to ensure that prepared statements are reset when
   leaving the scope that uses them.  Unfinished prepared statements
   prevent transactions from being aborted, and can cause locks to be
   kept when they should be released. */
struct SQLiteStmtUse
{
    SQLiteStmt & stmt;
    SQLiteStmtUse(SQLiteStmt & stmt) : stmt(stmt)
    {
        stmt.reset();
    }
    ~SQLiteStmtUse()
    {
        try {
            stmt.reset();
        } catch (...) {
            ignoreException();
        }
    }
};


struct SQLiteTxn 
{
    bool active;
    sqlite3 * db;
    
    SQLiteTxn(sqlite3 * db) : active(false) {
        this->db = db;
        if (sqlite3_exec(db, "begin;", 0, 0, 0) != SQLITE_OK)
            throwSQLiteError(db, "starting transaction");
        active = true;
    }

    void commit() 
    {
        if (sqlite3_exec(db, "commit;", 0, 0, 0) != SQLITE_OK)
            throwSQLiteError(db, "committing transaction");
        active = false;
    }
    
    ~SQLiteTxn() 
    {
        try {
            if (active && sqlite3_exec(db, "rollback;", 0, 0, 0) != SQLITE_OK)
                throwSQLiteError(db, "aborting transaction");
        } catch (...) {
            ignoreException();
        }
    }
};


void checkStoreNotSymlink()
{
    if (getEnv("NIX_IGNORE_SYMLINK_STORE") == "1") return;
    Path path = nixStore;
    struct stat st;
    while (path != "/") {
        if (lstat(path.c_str(), &st))
            throw SysError(format("getting status of `%1%'") % path);
        if (S_ISLNK(st.st_mode))
            throw Error(format(
                "the path `%1%' is a symlink; "
                "this is not allowed for the Nix store and its parent directories")
                % path);
        path = dirOf(path);
    }
}


LocalStore::LocalStore()
{
    substitutablePathsLoaded = false;
    
    schemaPath = nixDBPath + "/schema";
    
    if (readOnlyMode) {
        openDB(false);
        return;
    }

    /* Create missing state directories if they don't already exist. */
    createDirs(nixStore);
    Path profilesDir = nixStateDir + "/profiles";
    createDirs(nixStateDir + "/profiles");
    createDirs(nixStateDir + "/temproots");
    createDirs(nixDBPath);
    Path gcRootsDir = nixStateDir + "/gcroots";
    if (!pathExists(gcRootsDir)) {
        createDirs(gcRootsDir);
        if (symlink(profilesDir.c_str(), (gcRootsDir + "/profiles").c_str()) == -1)
            throw SysError(format("creating symlink to `%1%'") % profilesDir);
    }
  
    checkStoreNotSymlink();

    /* Acquire the big fat lock in shared mode to make sure that no
       schema upgrade is in progress. */
    try {
        Path globalLockPath = nixDBPath + "/big-lock";
        globalLock = openLockFile(globalLockPath.c_str(), true);
    } catch (SysError & e) {
        if (e.errNo != EACCES) throw;
        readOnlyMode = true;
        openDB(false);
        return;
    }
    
    if (!lockFile(globalLock, ltRead, false)) {
        printMsg(lvlError, "waiting for the big Nix store lock...");
        lockFile(globalLock, ltRead, true);
    }

    /* Check the current database schema and if necessary do an
       upgrade.  */
    int curSchema = getSchema();
    if (curSchema > nixSchemaVersion)
        throw Error(format("current Nix store schema is version %1%, but I only support %2%")
            % curSchema % nixSchemaVersion);
    
    else if (curSchema == 0) { /* new store */
        curSchema = nixSchemaVersion;
        openDB(true);
        writeFile(schemaPath, (format("%1%") % nixSchemaVersion).str());
    }
    
    else if (curSchema < nixSchemaVersion) {
        if (curSchema < 5)
            throw Error(
                "Your Nix store has a database in Berkeley DB format,\n"
                "which is no longer supported. To convert to the new format,\n"
                "please upgrade Nix to version 0.12 first.");
        
        if (!lockFile(globalLock, ltWrite, false)) {
            printMsg(lvlError, "waiting for exclusive access to the Nix store...");
            lockFile(globalLock, ltWrite, true);
        }

        /* Get the schema version again, because another process may
           have performed the upgrade already. */
        curSchema = getSchema();

        if (curSchema < 6) upgradeStore6();

        writeFile(schemaPath, (format("%1%") % nixSchemaVersion).str());

        lockFile(globalLock, ltRead, true);
    }
    
    else openDB(false);
}


LocalStore::~LocalStore()
{
    try {
        foreach (RunningSubstituters::iterator, i, runningSubstituters) {
            i->second.to.close();
            i->second.from.close();
            i->second.pid.wait(true);
        }
    } catch (...) {
        ignoreException();
    }
}


int LocalStore::getSchema()
{
    int curSchema = 0;
    if (pathExists(schemaPath)) {
        string s = readFile(schemaPath);
        if (!string2Int(s, curSchema))
            throw Error(format("`%1%' is corrupt") % schemaPath);
    }
    return curSchema;
}


void LocalStore::openDB(bool create)
{
    /* Open the Nix database. */
    if (sqlite3_open_v2((nixDBPath + "/db.sqlite").c_str(), &db.db,
            SQLITE_OPEN_READWRITE | (create ? SQLITE_OPEN_CREATE : 0), 0) != SQLITE_OK)
        throw Error("cannot open SQLite database");

    if (sqlite3_busy_timeout(db, 60 * 60 * 1000) != SQLITE_OK)
        throwSQLiteError(db, "setting timeout");

    if (sqlite3_exec(db, "pragma foreign_keys = 1;", 0, 0, 0) != SQLITE_OK)
        throwSQLiteError(db, "enabling foreign keys");

    /* !!! check whether sqlite has been built with foreign key
       support */
    
    /* Whether SQLite should fsync().  "Normal" synchronous mode
       should be safe enough.  If the user asks for it, don't sync at
       all.  This can cause database corruption if the system
       crashes. */
    string syncMode = queryBoolSetting("fsync-metadata", true) ? "normal" : "off";
    if (sqlite3_exec(db, ("pragma synchronous = " + syncMode + ";").c_str(), 0, 0, 0) != SQLITE_OK)
        throwSQLiteError(db, "setting synchronous mode");

    /* Set the SQLite journal mode.  WAL mode is fastest, so it's the
       default. */
    string mode = queryBoolSetting("use-sqlite-wal", true) ? "wal" : "truncate";
    string prevMode;
    {
        SQLiteStmt stmt;
        stmt.create(db, "pragma main.journal_mode;");
        if (sqlite3_step(stmt) != SQLITE_ROW)
            throwSQLiteError(db, "querying journal mode");
        prevMode = string((const char *) sqlite3_column_text(stmt, 0));
    }
    if (prevMode != mode &&
        sqlite3_exec(db, ("pragma main.journal_mode = " + mode + ";").c_str(), 0, 0, 0) != SQLITE_OK)
        throwSQLiteError(db, "setting journal mode");

    /* Increase the auto-checkpoint interval to 8192 pages.  This
       seems enough to ensure that instantiating the NixOS system
       derivation is done in a single fsync(). */
    if (mode == "wal" && sqlite3_exec(db, "pragma wal_autocheckpoint = 8192;", 0, 0, 0) != SQLITE_OK)
        throwSQLiteError(db, "setting autocheckpoint interval");
    
    /* Initialise the database schema, if necessary. */
    if (create) {
#include "schema.sql.hh"
        if (sqlite3_exec(db, (const char *) schema, 0, 0, 0) != SQLITE_OK)
            throwSQLiteError(db, "initialising database schema");
    }

    /* Backwards compatibility with old (pre-release) databases.  Can
       remove this eventually. */
    if (sqlite3_table_column_metadata(db, 0, "ValidPaths", "narSize", 0, 0, 0, 0, 0) != SQLITE_OK) {
        if (sqlite3_exec(db, "alter table ValidPaths add column narSize integer" , 0, 0, 0) != SQLITE_OK)
            throwSQLiteError(db, "adding column narSize");
    }

    /* Prepare SQL statements. */
    stmtRegisterValidPath.create(db,
        "insert into ValidPaths (path, hash, registrationTime, deriver, narSize) values (?, ?, ?, ?, ?);");
    stmtUpdatePathInfo.create(db,
        "update ValidPaths set narSize = ? where path = ?;");
    stmtAddReference.create(db,
        "insert or replace into Refs (referrer, reference) values (?, ?);");
    stmtQueryPathInfo.create(db,
        "select id, hash, registrationTime, deriver, narSize from ValidPaths where path = ?;");
    stmtQueryReferences.create(db,
        "select path from Refs join ValidPaths on reference = id where referrer = ?;");
    stmtQueryReferrers.create(db,
        "select path from Refs join ValidPaths on referrer = id where reference = (select id from ValidPaths where path = ?);");
    stmtInvalidatePath.create(db,
        "delete from ValidPaths where path = ?;");
    stmtRegisterFailedPath.create(db,
        "insert into FailedPaths (path, time) values (?, ?);");
    stmtHasPathFailed.create(db,
        "select time from FailedPaths where path = ?;");
    stmtQueryFailedPaths.create(db,
        "select path from FailedPaths;");
    stmtClearFailedPath.create(db,
        "delete from FailedPaths where ?1 = '*' or path = ?1;");
    stmtAddDerivationOutput.create(db,
        "insert or replace into DerivationOutputs (drv, id, path) values (?, ?, ?);");
    stmtQueryValidDerivers.create(db,
        "select v.id, v.path from DerivationOutputs d join ValidPaths v on d.drv = v.id where d.path = ?;");
    stmtQueryDerivationOutputs.create(db,
        "select id, path from DerivationOutputs where drv = ?;");
}


void canonicalisePathMetaData(const Path & path, bool recurse)
{
    checkInterrupt();

    struct stat st;
    if (lstat(path.c_str(), &st))
	throw SysError(format("getting attributes of path `%1%'") % path);

    /* Change ownership to the current uid.  If it's a symlink, use
       lchown if available, otherwise don't bother.  Wrong ownership
       of a symlink doesn't matter, since the owning user can't change
       the symlink and can't delete it because the directory is not
       writable.  The only exception is top-level paths in the Nix
       store (since that directory is group-writable for the Nix build
       users group); we check for this case below. */
    if (st.st_uid != geteuid()) {
#if HAVE_LCHOWN
        if (lchown(path.c_str(), geteuid(), (gid_t) -1) == -1)
#else
        if (!S_ISLNK(st.st_mode) &&
            chown(path.c_str(), geteuid(), (gid_t) -1) == -1)
#endif
            throw SysError(format("changing owner of `%1%' to %2%")
                % path % geteuid());
    }
    
    if (!S_ISLNK(st.st_mode)) {

        /* Mask out all type related bits. */
        mode_t mode = st.st_mode & ~S_IFMT;
        
        if (mode != 0444 && mode != 0555) {
            mode = (st.st_mode & S_IFMT)
                 | 0444
                 | (st.st_mode & S_IXUSR ? 0111 : 0);
            if (chmod(path.c_str(), mode) == -1)
                throw SysError(format("changing mode of `%1%' to %2$o") % path % mode);
        }

        if (st.st_mtime != 0) {
            struct utimbuf utimbuf;
            utimbuf.actime = st.st_atime;
            utimbuf.modtime = 1; /* 1 second into the epoch */
            if (utime(path.c_str(), &utimbuf) == -1) 
                throw SysError(format("changing modification time of `%1%'") % path);
        }

    }

    if (recurse && S_ISDIR(st.st_mode)) {
        Strings names = readDirectory(path);
	foreach (Strings::iterator, i, names)
	    canonicalisePathMetaData(path + "/" + *i, true);
    }
}


void canonicalisePathMetaData(const Path & path)
{
    canonicalisePathMetaData(path, true);

    /* On platforms that don't have lchown(), the top-level path can't
       be a symlink, since we can't change its ownership. */
    struct stat st;
    if (lstat(path.c_str(), &st))
	throw SysError(format("getting attributes of path `%1%'") % path);

    if (st.st_uid != geteuid()) {
        assert(S_ISLNK(st.st_mode));
        throw Error(format("wrong ownership of top-level store path `%1%'") % path);
    }
}


void LocalStore::checkDerivationOutputs(const Path & drvPath, const Derivation & drv)
{
    string drvName = storePathToName(drvPath);
    assert(isDerivation(drvName));
    drvName = string(drvName, 0, drvName.size() - drvExtension.size());
        
    if (isFixedOutputDrv(drv)) {
        DerivationOutputs::const_iterator out = drv.outputs.find("out");
        if (out == drv.outputs.end())
            throw Error(format("derivation `%1%' does not have an output named `out'") % drvPath);

        bool recursive; HashType ht; Hash h;
        out->second.parseHashInfo(recursive, ht, h);
        Path outPath = makeFixedOutputPath(recursive, ht, h, drvName);

        StringPairs::const_iterator j = drv.env.find("out");
        if (out->second.path != outPath || j == drv.env.end() || j->second != outPath)
            throw Error(format("derivation `%1%' has incorrect output `%2%', should be `%3%'")
                % drvPath % out->second.path % outPath);
    }

    else {
        Derivation drvCopy(drv);
        foreach (DerivationOutputs::iterator, i, drvCopy.outputs) {
            i->second.path = "";
            drvCopy.env[i->first] = "";
        }

        Hash h = hashDerivationModulo(*this, drvCopy);
        
        foreach (DerivationOutputs::const_iterator, i, drv.outputs) {
            Path outPath = makeOutputPath(i->first, h, drvName);
            StringPairs::const_iterator j = drv.env.find(i->first);
            if (i->second.path != outPath || j == drv.env.end() || j->second != outPath)
                throw Error(format("derivation `%1%' has incorrect output `%2%', should be `%3%'")
                    % drvPath % i->second.path % outPath);
        }
    }
}


unsigned long long LocalStore::addValidPath(const ValidPathInfo & info, bool checkOutputs)
{
    SQLiteStmtUse use(stmtRegisterValidPath);
    stmtRegisterValidPath.bind(info.path);
    stmtRegisterValidPath.bind("sha256:" + printHash(info.hash));
    stmtRegisterValidPath.bind(info.registrationTime == 0 ? time(0) : info.registrationTime);
    if (info.deriver != "")
        stmtRegisterValidPath.bind(info.deriver);
    else
        stmtRegisterValidPath.bind(); // null
    if (info.narSize != 0)
        stmtRegisterValidPath.bind64(info.narSize);
    else
        stmtRegisterValidPath.bind(); // null
    if (sqlite3_step(stmtRegisterValidPath) != SQLITE_DONE)
        throwSQLiteError(db, format("registering valid path `%1%' in database") % info.path);
    unsigned long long id = sqlite3_last_insert_rowid(db);

    /* If this is a derivation, then store the derivation outputs in
       the database.  This is useful for the garbage collector: it can
       efficiently query whether a path is an output of some
       derivation. */
    if (isDerivation(info.path)) {
        Derivation drv = parseDerivation(readFile(info.path));
        
        /* Verify that the output paths in the derivation are correct
           (i.e., follow the scheme for computing output paths from
           derivations).  Note that if this throws an error, then the
           DB transaction is rolled back, so the path validity
           registration above is undone. */
        if (checkOutputs) checkDerivationOutputs(info.path, drv);
        
        foreach (DerivationOutputs::iterator, i, drv.outputs) {
            SQLiteStmtUse use(stmtAddDerivationOutput);
            stmtAddDerivationOutput.bind(id);
            stmtAddDerivationOutput.bind(i->first);
            stmtAddDerivationOutput.bind(i->second.path);
            if (sqlite3_step(stmtAddDerivationOutput) != SQLITE_DONE)
                throwSQLiteError(db, format("adding derivation output for `%1%' in database") % info.path);
        }
    }

    return id;
}


void LocalStore::addReference(unsigned long long referrer, unsigned long long reference)
{
    SQLiteStmtUse use(stmtAddReference);
    stmtAddReference.bind(referrer);
    stmtAddReference.bind(reference);
    if (sqlite3_step(stmtAddReference) != SQLITE_DONE)
        throwSQLiteError(db, "adding reference to database");
}


void LocalStore::registerFailedPath(const Path & path)
{
    if (hasPathFailed(path)) return;
    SQLiteStmtUse use(stmtRegisterFailedPath);
    stmtRegisterFailedPath.bind(path);
    stmtRegisterFailedPath.bind(time(0));
    if (sqlite3_step(stmtRegisterFailedPath) != SQLITE_DONE)
        throwSQLiteError(db, format("registering failed path `%1%'") % path);
}


bool LocalStore::hasPathFailed(const Path & path)
{
    SQLiteStmtUse use(stmtHasPathFailed);
    stmtHasPathFailed.bind(path);
    int res = sqlite3_step(stmtHasPathFailed);
    if (res != SQLITE_DONE && res != SQLITE_ROW)
        throwSQLiteError(db, "querying whether path failed");
    return res == SQLITE_ROW;
}


PathSet LocalStore::queryFailedPaths()
{
    SQLiteStmtUse use(stmtQueryFailedPaths);

    PathSet res;
    int r;
    while ((r = sqlite3_step(stmtQueryFailedPaths)) == SQLITE_ROW) {
        const char * s = (const char *) sqlite3_column_text(stmtQueryFailedPaths, 0);
        assert(s);
        res.insert(s);
    }

    if (r != SQLITE_DONE)
        throwSQLiteError(db, "error querying failed paths");

    return res;
}


void LocalStore::clearFailedPaths(const PathSet & paths)
{
    SQLiteTxn txn(db);

    foreach (PathSet::const_iterator, i, paths) {
        SQLiteStmtUse use(stmtClearFailedPath);
        stmtClearFailedPath.bind(*i);
        if (sqlite3_step(stmtClearFailedPath) != SQLITE_DONE)
            throwSQLiteError(db, format("clearing failed path `%1%' in database") % *i);
    }

    txn.commit();
}


Hash parseHashField(const Path & path, const string & s)
{
    string::size_type colon = s.find(':');
    if (colon == string::npos)
        throw Error(format("corrupt hash `%1%' in valid-path entry for `%2%'")
            % s % path);
    HashType ht = parseHashType(string(s, 0, colon));
    if (ht == htUnknown)
        throw Error(format("unknown hash type `%1%' in valid-path entry for `%2%'")
            % string(s, 0, colon) % path);
    return parseHash(ht, string(s, colon + 1));
}


ValidPathInfo LocalStore::queryPathInfo(const Path & path)
{
    ValidPathInfo info;
    info.path = path;

    assertStorePath(path);

    /* Get the path info. */
    SQLiteStmtUse use1(stmtQueryPathInfo);

    stmtQueryPathInfo.bind(path);
    
    int r = sqlite3_step(stmtQueryPathInfo);
    if (r == SQLITE_DONE) throw Error(format("path `%1%' is not valid") % path);
    if (r != SQLITE_ROW) throwSQLiteError(db, "querying path in database");

    info.id = sqlite3_column_int(stmtQueryPathInfo, 0);

    const char * s = (const char *) sqlite3_column_text(stmtQueryPathInfo, 1);
    assert(s);
    info.hash = parseHashField(path, s);
    
    info.registrationTime = sqlite3_column_int(stmtQueryPathInfo, 2);

    s = (const char *) sqlite3_column_text(stmtQueryPathInfo, 3);
    if (s) info.deriver = s;

    /* Note that narSize = NULL yields 0. */
    info.narSize = sqlite3_column_int64(stmtQueryPathInfo, 4);

    /* Get the references. */
    SQLiteStmtUse use2(stmtQueryReferences);

    stmtQueryReferences.bind(info.id);

    while ((r = sqlite3_step(stmtQueryReferences)) == SQLITE_ROW) {
        s = (const char *) sqlite3_column_text(stmtQueryReferences, 0);
        assert(s);
        info.references.insert(s);
    }

    if (r != SQLITE_DONE)
        throwSQLiteError(db, format("error getting references of `%1%'") % path);

    return info;
}


/* Update path info in the database.  Currently only updated the
   narSize field. */
void LocalStore::updatePathInfo(const ValidPathInfo & info)
{
    SQLiteStmtUse use(stmtUpdatePathInfo);
    if (info.narSize != 0)
        stmtUpdatePathInfo.bind64(info.narSize);
    else
        stmtUpdatePathInfo.bind(); // null
    stmtUpdatePathInfo.bind(info.path);
    if (sqlite3_step(stmtUpdatePathInfo) != SQLITE_DONE)
        throwSQLiteError(db, format("updating info of path `%1%' in database") % info.path);
}


unsigned long long LocalStore::queryValidPathId(const Path & path)
{
    SQLiteStmtUse use(stmtQueryPathInfo);
    stmtQueryPathInfo.bind(path);
    int res = sqlite3_step(stmtQueryPathInfo);
    if (res == SQLITE_ROW) return sqlite3_column_int(stmtQueryPathInfo, 0);
    if (res == SQLITE_DONE) throw Error(format("path `%1%' is not valid") % path);
    throwSQLiteError(db, "querying path in database");
}


bool LocalStore::isValidPath(const Path & path)
{
    SQLiteStmtUse use(stmtQueryPathInfo);
    stmtQueryPathInfo.bind(path);
    int res = sqlite3_step(stmtQueryPathInfo);
    if (res != SQLITE_DONE && res != SQLITE_ROW)
        throwSQLiteError(db, "querying path in database");
    return res == SQLITE_ROW;
}


PathSet LocalStore::queryValidPaths()
{
    SQLiteStmt stmt;
    stmt.create(db, "select path from ValidPaths");
    
    PathSet res;
    
    int r;
    while ((r = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char * s = (const char *) sqlite3_column_text(stmt, 0);
        assert(s);
        res.insert(s);
    }

    if (r != SQLITE_DONE)
        throwSQLiteError(db, "error getting valid paths");

    return res;
}


void LocalStore::queryReferences(const Path & path,
    PathSet & references)
{
    ValidPathInfo info = queryPathInfo(path);
    references.insert(info.references.begin(), info.references.end());
}


void LocalStore::queryReferrers(const Path & path, PathSet & referrers)
{
    assertStorePath(path);

    SQLiteStmtUse use(stmtQueryReferrers);

    stmtQueryReferrers.bind(path);

    int r;
    while ((r = sqlite3_step(stmtQueryReferrers)) == SQLITE_ROW) {
        const char * s = (const char *) sqlite3_column_text(stmtQueryReferrers, 0);
        assert(s);
        referrers.insert(s);
    }

    if (r != SQLITE_DONE)
        throwSQLiteError(db, format("error getting references of `%1%'") % path);
}


Path LocalStore::queryDeriver(const Path & path)
{
    return queryPathInfo(path).deriver;
}


PathSet LocalStore::queryValidDerivers(const Path & path)
{
    assertStorePath(path);

    SQLiteStmtUse use(stmtQueryValidDerivers);
    stmtQueryValidDerivers.bind(path);

    PathSet derivers;
    int r;
    while ((r = sqlite3_step(stmtQueryValidDerivers)) == SQLITE_ROW) {
        const char * s = (const char *) sqlite3_column_text(stmtQueryValidDerivers, 1);
        assert(s);
        derivers.insert(s);
    }
    
    if (r != SQLITE_DONE)
        throwSQLiteError(db, format("error getting valid derivers of `%1%'") % path);
    
    return derivers;
}


PathSet LocalStore::queryDerivationOutputs(const Path & path)
{
    SQLiteTxn txn(db);
    
    SQLiteStmtUse use(stmtQueryDerivationOutputs);
    stmtQueryDerivationOutputs.bind(queryValidPathId(path));
    
    PathSet outputs;
    int r;
    while ((r = sqlite3_step(stmtQueryDerivationOutputs)) == SQLITE_ROW) {
        const char * s = (const char *) sqlite3_column_text(stmtQueryDerivationOutputs, 1);
        assert(s);
        outputs.insert(s);
    }
    
    if (r != SQLITE_DONE)
        throwSQLiteError(db, format("error getting outputs of `%1%'") % path);

    return outputs;
}


void LocalStore::startSubstituter(const Path & substituter, RunningSubstituter & run)
{
    if (run.pid != -1) return;
    
    debug(format("starting substituter program `%1%'") % substituter);

    Pipe toPipe, fromPipe;
            
    toPipe.create();
    fromPipe.create();

    run.pid = fork();
            
    switch (run.pid) {

    case -1:
        throw SysError("unable to fork");

    case 0: /* child */
        try {
            /* Hack to let "make check" succeed on Darwin.  The
               libtool wrapper script sets DYLD_LIBRARY_PATH to our
               libutil (among others), but Perl also depends on a
               library named libutil.  As a result, substituters
               written in Perl (i.e. all of them) fail. */
            unsetenv("DYLD_LIBRARY_PATH");
            
            fromPipe.readSide.close();
            toPipe.writeSide.close();
            if (dup2(toPipe.readSide, STDIN_FILENO) == -1)
                throw SysError("dupping stdin");
            if (dup2(fromPipe.writeSide, STDOUT_FILENO) == -1)
                throw SysError("dupping stdout");
            closeMostFDs(set<int>());
            execl(substituter.c_str(), substituter.c_str(), "--query", NULL);
            throw SysError(format("executing `%1%'") % substituter);
        } catch (std::exception & e) {
            std::cerr << "error: " << e.what() << std::endl;
        }
        quickExit(1);
    }

    /* Parent. */
    
    run.to = toPipe.writeSide.borrow();
    run.from = fromPipe.readSide.borrow();
}


template<class T> T getIntLine(int fd)
{
    string s = readLine(fd);
    T res;
    if (!string2Int(s, res)) throw Error("integer expected from stream");
    return res;
}


bool LocalStore::hasSubstitutes(const Path & path)
{
    foreach (Paths::iterator, i, substituters) {
        RunningSubstituter & run(runningSubstituters[*i]);
        startSubstituter(*i, run);
        writeLine(run.to, "have\n" + path);
        if (getIntLine<int>(run.from)) return true;
    }

    return false;
}


bool LocalStore::querySubstitutablePathInfo(const Path & substituter,
    const Path & path, SubstitutablePathInfo & info)
{
    RunningSubstituter & run(runningSubstituters[substituter]);
    startSubstituter(substituter, run);

    writeLine(run.to, "info\n" + path);

    if (!getIntLine<int>(run.from)) return false;
    
    info.deriver = readLine(run.from);
    if (info.deriver != "") assertStorePath(info.deriver);
    int nrRefs = getIntLine<int>(run.from);
    while (nrRefs--) {
        Path p = readLine(run.from);
        assertStorePath(p);
        info.references.insert(p);
    }
    info.downloadSize = getIntLine<long long>(run.from);
    info.narSize = getIntLine<long long>(run.from);
    
    return true;
}


bool LocalStore::querySubstitutablePathInfo(const Path & path,
    SubstitutablePathInfo & info)
{
    foreach (Paths::iterator, i, substituters)
        if (querySubstitutablePathInfo(*i, path, info)) return true;
    return false;
}


Hash LocalStore::queryPathHash(const Path & path)
{
    return queryPathInfo(path).hash;
}


void LocalStore::registerValidPath(const ValidPathInfo & info)
{
    ValidPathInfos infos;
    infos.push_back(info);
    registerValidPaths(infos);
}


void LocalStore::registerValidPaths(const ValidPathInfos & infos)
{
    while (1) {
        try {
            SQLiteTxn txn(db);
    
            foreach (ValidPathInfos::const_iterator, i, infos) {
                assert(i->hash.type == htSHA256);
                /* !!! Maybe the registration info should be updated if the
                   path is already valid. */
                if (!isValidPath(i->path)) addValidPath(*i);
            }

            foreach (ValidPathInfos::const_iterator, i, infos) {
                unsigned long long referrer = queryValidPathId(i->path);
                foreach (PathSet::iterator, j, i->references)
                    addReference(referrer, queryValidPathId(*j));
            }

            txn.commit();
            break;
        } catch (SQLiteBusy & e) {
            /* Retry; the `txn' destructor will roll back the current
               transaction. */
        }
    }
}


/* Invalidate a path.  The caller is responsible for checking that
   there are no referrers. */
void LocalStore::invalidatePath(const Path & path)
{
    debug(format("invalidating path `%1%'") % path);

    drvHashes.erase(path);

    SQLiteStmtUse use(stmtInvalidatePath);

    stmtInvalidatePath.bind(path);

    if (sqlite3_step(stmtInvalidatePath) != SQLITE_DONE)
        throwSQLiteError(db, format("invalidating path `%1%' in database") % path);

    /* Note that the foreign key constraints on the Refs table take
       care of deleting the references entries for `path'. */
}


Path LocalStore::addToStoreFromDump(const string & dump, const string & name,
    bool recursive, HashType hashAlgo)
{
    Hash h = hashString(hashAlgo, dump);

    Path dstPath = makeFixedOutputPath(recursive, hashAlgo, h, name);

    addTempRoot(dstPath);

    if (!isValidPath(dstPath)) {

        /* The first check above is an optimisation to prevent
           unnecessary lock acquisition. */

        PathLocks outputLock(singleton<PathSet, Path>(dstPath));

        if (!isValidPath(dstPath)) {

            if (pathExists(dstPath)) deletePathWrapped(dstPath);

            if (recursive) {
                StringSource source(dump);
                restorePath(dstPath, source);
            } else
                writeFile(dstPath, dump);

            canonicalisePathMetaData(dstPath);

            /* Register the SHA-256 hash of the NAR serialisation of
               the path in the database.  We may just have computed it
               above (if called with recursive == true and hashAlgo ==
               sha256); otherwise, compute it here. */
            HashResult hash;
            if (recursive) {
                hash.first = hashAlgo == htSHA256 ? h : hashString(htSHA256, dump);
                hash.second = dump.size();
            } else
                hash = hashPath(htSHA256, dstPath);
            
            ValidPathInfo info;
            info.path = dstPath;
            info.hash = hash.first;
            info.narSize = hash.second;
            registerValidPath(info);
        }

        outputLock.setDeletion(true);
    }

    return dstPath;
}


Path LocalStore::addToStore(const Path & _srcPath,
    bool recursive, HashType hashAlgo, PathFilter & filter)
{
    Path srcPath(absPath(_srcPath));
    debug(format("adding `%1%' to the store") % srcPath);

    /* Read the whole path into memory. This is not a very scalable
       method for very large paths, but `copyPath' is mainly used for
       small files. */
    StringSink sink;
    if (recursive) 
        dumpPath(srcPath, sink, filter);
    else
        sink.s = readFile(srcPath);

    return addToStoreFromDump(sink.s, baseNameOf(srcPath), recursive, hashAlgo);
}


Path LocalStore::addTextToStore(const string & name, const string & s,
    const PathSet & references)
{
    Path dstPath = computeStorePathForText(name, s, references);
    
    addTempRoot(dstPath);

    if (!isValidPath(dstPath)) {

        PathLocks outputLock(singleton<PathSet, Path>(dstPath));

        if (!isValidPath(dstPath)) {

            if (pathExists(dstPath)) deletePathWrapped(dstPath);

            writeFile(dstPath, s);

            canonicalisePathMetaData(dstPath);

            HashResult hash = hashPath(htSHA256, dstPath);
            
            ValidPathInfo info;
            info.path = dstPath;
            info.hash = hash.first;
            info.narSize = hash.second;
            info.references = references;
            registerValidPath(info);
        }

        outputLock.setDeletion(true);
    }

    return dstPath;
}


struct HashAndWriteSink : Sink
{
    Sink & writeSink;
    HashSink hashSink;
    HashAndWriteSink(Sink & writeSink) : writeSink(writeSink), hashSink(htSHA256)
    {
    }
    virtual void operator ()
        (const unsigned char * data, unsigned int len)
    {
        writeSink(data, len);
        hashSink(data, len);
    }
    Hash currentHash()
    {
        HashSink hashSinkClone(hashSink);
        return hashSinkClone.finish().first;
    }
};


#define EXPORT_MAGIC 0x4558494e


static void checkSecrecy(const Path & path)
{
    struct stat st;
    if (stat(path.c_str(), &st))
        throw SysError(format("getting status of `%1%'") % path);
    if ((st.st_mode & (S_IRWXG | S_IRWXO)) != 0)
        throw Error(format("file `%1%' should be secret (inaccessible to everybody else)!") % path);
}


void LocalStore::exportPath(const Path & path, bool sign,
    Sink & sink)
{
    assertStorePath(path);

    addTempRoot(path);
    if (!isValidPath(path))
        throw Error(format("path `%1%' is not valid") % path);

    HashAndWriteSink hashAndWriteSink(sink);
    
    dumpPath(path, hashAndWriteSink);

    /* Refuse to export paths that have changed.  This prevents
       filesystem corruption from spreading to other machines.
       Don't complain if the stored hash is zero (unknown). */
    Hash hash = hashAndWriteSink.currentHash();
    Hash storedHash = queryPathHash(path);
    if (hash != storedHash && storedHash != Hash(storedHash.type))
        throw Error(format("hash of path `%1%' has changed from `%2%' to `%3%'!") % path
            % printHash(storedHash) % printHash(hash));

    writeInt(EXPORT_MAGIC, hashAndWriteSink);

    writeString(path, hashAndWriteSink);
    
    PathSet references;
    queryReferences(path, references);
    writeStringSet(references, hashAndWriteSink);

    Path deriver = queryDeriver(path);
    writeString(deriver, hashAndWriteSink);

    if (sign) {
        Hash hash = hashAndWriteSink.currentHash();
 
        writeInt(1, hashAndWriteSink);
        
        Path tmpDir = createTempDir();
        AutoDelete delTmp(tmpDir);
        Path hashFile = tmpDir + "/hash";
        writeFile(hashFile, printHash(hash));

        Path secretKey = nixConfDir + "/signing-key.sec";
        checkSecrecy(secretKey);

        Strings args;
        args.push_back("rsautl");
        args.push_back("-sign");
        args.push_back("-inkey");
        args.push_back(secretKey);
        args.push_back("-in");
        args.push_back(hashFile);
        string signature = runProgram(OPENSSL_PATH, true, args);

        writeString(signature, hashAndWriteSink);
        
    } else
        writeInt(0, hashAndWriteSink);
}


struct HashAndReadSource : Source
{
    Source & readSource;
    HashSink hashSink;
    bool hashing;
    HashAndReadSource(Source & readSource) : readSource(readSource), hashSink(htSHA256)
    {
        hashing = true;
    }
    virtual void operator ()
        (unsigned char * data, unsigned int len)
    {
        readSource(data, len);
        if (hashing) hashSink(data, len);
    }
};


/* Create a temporary directory in the store that won't be
   garbage-collected. */
Path LocalStore::createTempDirInStore()
{
    Path tmpDir;
    do {
        /* There is a slight possibility that `tmpDir' gets deleted by
           the GC between createTempDir() and addTempRoot(), so repeat
           until `tmpDir' exists. */
        tmpDir = createTempDir(nixStore);
        addTempRoot(tmpDir);
    } while (!pathExists(tmpDir));
    return tmpDir;
}


Path LocalStore::importPath(bool requireSignature, Source & source)
{
    HashAndReadSource hashAndReadSource(source);
    
    /* We don't yet know what store path this archive contains (the
       store path follows the archive data proper), and besides, we
       don't know yet whether the signature is valid. */
    Path tmpDir = createTempDirInStore();
    AutoDelete delTmp(tmpDir);
    Path unpacked = tmpDir + "/unpacked";

    restorePath(unpacked, hashAndReadSource);

    unsigned int magic = readInt(hashAndReadSource);
    if (magic != EXPORT_MAGIC)
        throw Error("Nix archive cannot be imported; wrong format");

    Path dstPath = readStorePath(hashAndReadSource);

    PathSet references = readStorePaths(hashAndReadSource);

    Path deriver = readString(hashAndReadSource);
    if (deriver != "") assertStorePath(deriver);

    Hash hash = hashAndReadSource.hashSink.finish().first;
    hashAndReadSource.hashing = false;

    bool haveSignature = readInt(hashAndReadSource) == 1;

    if (requireSignature && !haveSignature)
        throw Error(format("imported archive of `%1%' lacks a signature") % dstPath);
    
    if (haveSignature) {
        string signature = readString(hashAndReadSource);

        if (requireSignature) {
            Path sigFile = tmpDir + "/sig";
            writeFile(sigFile, signature);

            Strings args;
            args.push_back("rsautl");
            args.push_back("-verify");
            args.push_back("-inkey");
            args.push_back(nixConfDir + "/signing-key.pub");
            args.push_back("-pubin");
            args.push_back("-in");
            args.push_back(sigFile);
            string hash2 = runProgram(OPENSSL_PATH, true, args);

            /* Note: runProgram() throws an exception if the signature
               is invalid. */

            if (printHash(hash) != hash2)
                throw Error(
                    "signed hash doesn't match actual contents of imported "
                    "archive; archive could be corrupt, or someone is trying "
                    "to import a Trojan horse");
        }
    }

    /* Do the actual import. */

    /* !!! way too much code duplication with addTextToStore() etc. */
    addTempRoot(dstPath);

    if (!isValidPath(dstPath)) {

        PathLocks outputLock;

        /* Lock the output path.  But don't lock if we're being called
           from a build hook (whose parent process already acquired a
           lock on this path). */
        Strings locksHeld = tokenizeString(getEnv("NIX_HELD_LOCKS"));
        if (find(locksHeld.begin(), locksHeld.end(), dstPath) == locksHeld.end())
            outputLock.lockPaths(singleton<PathSet, Path>(dstPath));

        if (!isValidPath(dstPath)) {

            if (pathExists(dstPath)) deletePathWrapped(dstPath);

            if (rename(unpacked.c_str(), dstPath.c_str()) == -1)
                throw SysError(format("cannot move `%1%' to `%2%'")
                    % unpacked % dstPath);

            canonicalisePathMetaData(dstPath);
            
            /* !!! if we were clever, we could prevent the hashPath()
               here. */
            HashResult hash = hashPath(htSHA256, dstPath);
            
            ValidPathInfo info;
            info.path = dstPath;
            info.hash = hash.first;
            info.narSize = hash.second;
            info.references = references;
            info.deriver = deriver != "" && isValidPath(deriver) ? deriver : "";
            registerValidPath(info);
        }
        
        outputLock.setDeletion(true);
    }
    
    return dstPath;
}


void LocalStore::deleteFromStore(const Path & path, unsigned long long & bytesFreed,
    unsigned long long & blocksFreed)
{
    bytesFreed = 0;

    assertStorePath(path);

    while (1) {
        try {
            SQLiteTxn txn(db);

            if (isValidPath(path)) {
                PathSet referrers; queryReferrers(path, referrers);
                referrers.erase(path); /* ignore self-references */
                if (!referrers.empty())
                    throw PathInUse(format("cannot delete path `%1%' because it is in use by `%2%'")
                        % path % showPaths(referrers));
                invalidatePath(path);
            }

            txn.commit();
            break;
        } catch (SQLiteBusy & e) { };
    }
    
    deletePathWrapped(path, bytesFreed, blocksFreed);
}


void LocalStore::verifyStore(bool checkContents)
{
    printMsg(lvlError, format("reading the Nix store..."));

    /* Acquire the global GC lock to prevent a garbage collection. */
    AutoCloseFD fdGCLock = openGCLock(ltWrite);
    
    Paths entries = readDirectory(nixStore);
    PathSet store(entries.begin(), entries.end());

    /* Check whether all valid paths actually exist. */
    printMsg(lvlInfo, "checking path existence...");

    PathSet validPaths2 = queryValidPaths(), validPaths, done;

    foreach (PathSet::iterator, i, validPaths2)
        verifyPath(*i, store, done, validPaths);

    /* Release the GC lock so that checking content hashes (which can
       take ages) doesn't block the GC or builds. */
    fdGCLock.close();

    /* Optionally, check the content hashes (slow). */
    if (checkContents) {
        printMsg(lvlInfo, "checking hashes...");

        foreach (PathSet::iterator, i, validPaths) {
            try {
                ValidPathInfo info = queryPathInfo(*i);

                /* Check the content hash (optionally - slow). */
                printMsg(lvlTalkative, format("checking contents of `%1%'") % *i);
                HashResult current = hashPath(info.hash.type, *i);
                
                if (current.first != info.hash) {
                    printMsg(lvlError, format("path `%1%' was modified! "
                            "expected hash `%2%', got `%3%'")
                        % *i % printHash(info.hash) % printHash(current.first));
                } else {
                    /* Fill in missing narSize fields (from old stores). */
                    if (info.narSize == 0) {
                        printMsg(lvlError, format("updating size field on `%1%' to %2%") % *i % current.second);
                        info.narSize = current.second;
                        updatePathInfo(info);
                    }
                }
            
            } catch (Error & e) {
                /* It's possible that the path got GC'ed, so ignore
                   errors on invalid paths. */
                if (isValidPath(*i)) throw;
                printMsg(lvlError, format("warning: %1%") % e.msg());
            }
        }
    }
}


void LocalStore::verifyPath(const Path & path, const PathSet & store,
    PathSet & done, PathSet & validPaths)
{
    checkInterrupt();
    
    if (done.find(path) != done.end()) return;
    done.insert(path);

    if (!isStorePath(path)) {
        printMsg(lvlError, format("path `%1%' is not in the Nix store") % path);
        invalidatePath(path);
        return;
    }

    if (store.find(baseNameOf(path)) == store.end()) {
        /* Check any referrers first.  If we can invalidate them
           first, then we can invalidate this path as well. */
        bool canInvalidate = true;
        PathSet referrers; queryReferrers(path, referrers);
        foreach (PathSet::iterator, i, referrers)
            if (*i != path) {
                verifyPath(*i, store, done, validPaths);
                if (validPaths.find(*i) != validPaths.end())
                    canInvalidate = false;
            }

        if (canInvalidate) {
            printMsg(lvlError, format("path `%1%' disappeared, removing from database...") % path);
            invalidatePath(path);
        } else
            printMsg(lvlError, format("path `%1%' disappeared, but it still has valid referrers!") % path);
        
        return;
    }
    
    validPaths.insert(path);
}


/* Functions for upgrading from the pre-SQLite database. */

PathSet LocalStore::queryValidPathsOld()
{
    PathSet paths;
    Strings entries = readDirectory(nixDBPath + "/info");
    foreach (Strings::iterator, i, entries)
        if (i->at(0) != '.') paths.insert(nixStore + "/" + *i);
    return paths;
}


ValidPathInfo LocalStore::queryPathInfoOld(const Path & path)
{
    ValidPathInfo res;
    res.path = path;

    /* Read the info file. */
    string baseName = baseNameOf(path);
    Path infoFile = (format("%1%/info/%2%") % nixDBPath % baseName).str();
    if (!pathExists(infoFile))
        throw Error(format("path `%1%' is not valid") % path);
    string info = readFile(infoFile);

    /* Parse it. */
    Strings lines = tokenizeString(info, "\n");

    foreach (Strings::iterator, i, lines) {
        string::size_type p = i->find(':');
        if (p == string::npos)
            throw Error(format("corrupt line in `%1%': %2%") % infoFile % *i);
        string name(*i, 0, p);
        string value(*i, p + 2);
        if (name == "References") {
            Strings refs = tokenizeString(value, " ");
            res.references = PathSet(refs.begin(), refs.end());
        } else if (name == "Deriver") {
            res.deriver = value;
        } else if (name == "Hash") {
            res.hash = parseHashField(path, value);
        } else if (name == "Registered-At") {
            int n = 0;
            string2Int(value, n);
            res.registrationTime = n;
        }
    }

    return res;
}


/* Upgrade from schema 5 (Nix 0.12) to schema 6 (Nix >= 0.15). */
void LocalStore::upgradeStore6()
{
    printMsg(lvlError, "upgrading Nix store to new schema (this may take a while)...");

    openDB(true);

    PathSet validPaths = queryValidPathsOld();

    SQLiteTxn txn(db);
    
    foreach (PathSet::iterator, i, validPaths) {
        addValidPath(queryPathInfoOld(*i), false);
        std::cerr << ".";
    }

    std::cerr << "|";
    
    foreach (PathSet::iterator, i, validPaths) {
        ValidPathInfo info = queryPathInfoOld(*i);
        unsigned long long referrer = queryValidPathId(*i);
        foreach (PathSet::iterator, j, info.references)
            addReference(referrer, queryValidPathId(*j));
        std::cerr << ".";
    }

    std::cerr << "\n";

    txn.commit();
}


}
