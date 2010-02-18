#include "config.h"
#include "local-store.hh"
#include "globals.hh"
#include "archive.hh"
#include "pathlocks.hh"
#include "aterm.hh"
#include "derivations-ast.hh"
#include "worker-protocol.hh"
    
#include <iostream>
#include <algorithm>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>

#include <sqlite3.h>


namespace nix {

    
class SQLiteError : public Error
{
public:
    SQLiteError(sqlite3 * db, const format & f)
        : Error(format("%1%: %2%") % f.str() % sqlite3_errmsg(db))
    {
    }
};


SQLite::~SQLite()
{
    try {
        if (db && sqlite3_close(db) != SQLITE_OK)
            throw SQLiteError(db, "closing database");
    } catch (...) {
        ignoreException();
    }
}


void SQLiteStmt::create(sqlite3 * db, const string & s)
{
    assert(!stmt);
    if (sqlite3_prepare_v2(db, s.c_str(), -1, &stmt, 0) != SQLITE_OK)
        throw SQLiteError(db, "creating statement");
    this->db = db;
}


void SQLiteStmt::reset()
{
    assert(stmt);
    if (sqlite3_reset(stmt) != SQLITE_OK)
        throw SQLiteError(db, "resetting statement");
}


SQLiteStmt::~SQLiteStmt()
{
    try {
        if (stmt && sqlite3_finalize(stmt) != SQLITE_OK)
            throw SQLiteError(db, "finalizing statement");
    } catch (...) {
        ignoreException();
    }
}


struct SQLiteTxn 
{
    bool active;
    sqlite3 * db;
    
    SQLiteTxn(sqlite3 * db) : active(false) {
        this->db = db;
        if (sqlite3_exec(db, "begin;", 0, 0, 0) != SQLITE_OK)
            throw SQLiteError(db, "starting transaction");
        active = true;
    }

    void commit() 
    {
        if (sqlite3_exec(db, "commit;", 0, 0, 0) != SQLITE_OK)
            throw SQLiteError(db, "committing transaction");
        active = false;
    }
    
    ~SQLiteTxn() 
    {
        try {
            if (active && sqlite3_exec(db, "rollback;", 0, 0, 0) != SQLITE_OK)
                throw SQLiteError(db, "aborting transaction");
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
    
    if (readOnlyMode) return;

    /* Create missing state directories if they don't already exist. */
    createDirs(nixStore);
    Path profilesDir = nixStateDir + "/profiles";
    createDirs(nixStateDir + "/profiles");
    createDirs(nixStateDir + "/temproots");
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
        return;
    }
    
    if (!lockFile(globalLock, ltRead, false)) {
        printMsg(lvlError, "waiting for the big Nix store lock...");
        lockFile(globalLock, ltRead, true);
    }

    /* Open the Nix database. */
    if (sqlite3_open_v2((nixDBPath + "/db.sqlite").c_str(), &db.db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 0) != SQLITE_OK)
        throw Error("cannot open SQLite database");

    if (sqlite3_busy_timeout(db, 60000) != SQLITE_OK)
        throw SQLiteError(db, "setting timeout");
    
    /* Check the current database schema and if necessary do an
       upgrade.  !!! Race condition: several processes could start
       the upgrade at the same time. */
    int curSchema = getSchema();
    if (curSchema > nixSchemaVersion)
        throw Error(format("current Nix store schema is version %1%, but I only support %2%")
            % curSchema % nixSchemaVersion);
    if (curSchema == 0) { /* new store */
        curSchema = nixSchemaVersion;
        initSchema();
        writeFile(schemaPath, (format("%1%") % nixSchemaVersion).str());
    }
    else if (curSchema == 1) throw Error("your Nix store is no longer supported");
    else if (curSchema < 5)
        throw Error(
            "Your Nix store has a database in Berkeley DB format,\n"
            "which is no longer supported. To convert to the new format,\n"
            "please upgrade Nix to version 0.12 first.");
    else if (curSchema < 6) upgradeStore6();
    else prepareStatements();

    doFsync = queryBoolSetting("fsync-metadata", false);
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


#include "schema.sql.hh"

void LocalStore::initSchema()
{
    if (sqlite3_exec(db, (const char *) schema, 0, 0, 0) != SQLITE_OK)
        throw SQLiteError(db, "initialising database schema");

    prepareStatements();
}


void LocalStore::prepareStatements()
{
    stmtRegisterValidPath.create(db,
        "insert into ValidPaths (path, hash, registrationTime, deriver) values (?, ?, ?, ?);");
    stmtAddReference.create(db,
        "insert into Refs (referrer, reference) values (?, ?);");
    stmtIsValidPath.create(db, "select 1 from ValidPaths where path = ?;");
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


void LocalStore::registerValidPath(const Path & path,
    const Hash & hash, const PathSet & references,
    const Path & deriver)
{
    ValidPathInfo info;
    info.path = path;
    info.hash = hash;
    info.references = references;
    info.deriver = deriver;
    registerValidPath(info);
}


void LocalStore::registerValidPath(const ValidPathInfo & info, bool ignoreValidity)
{
#if 0    
    Path infoFile = infoFileFor(info.path);

    ValidPathInfo oldInfo;
    if (pathExists(infoFile)) oldInfo = queryPathInfo(info.path);

    /* Note that it's possible for infoFile to already exist. */

    /* Acquire a lock on each referrer file.  This prevents those
       paths from being invalidated.  (It would be a violation of the
       store invariants if we registered info.path as valid while some
       of its references are invalid.)  NB: there can be no deadlock
       here since we're acquiring the locks in sorted order. */
    PathSet lockNames;
    foreach (PathSet::const_iterator, i, info.references)
        if (*i != info.path) lockNames.insert(referrersFileFor(*i));
    PathLocks referrerLocks(lockNames);
    referrerLocks.setDeletion(true);
        
    string refs;
    foreach (PathSet::const_iterator, i, info.references) {
        if (!refs.empty()) refs += " ";
        refs += *i;

        if (!ignoreValidity && *i != info.path && !isValidPath(*i))
            throw Error(format("cannot register `%1%' as valid, because its reference `%2%' isn't valid")
                % info.path % *i);

        /* Update the referrer mapping for *i.  This must be done
           before the info file is written to maintain the invariant
           that if `path' is a valid path, then all its references
           have referrer mappings back to `path'.  A " " is prefixed
           to separate it from the previous entry.  It's not suffixed
           to deal with interrupted partial writes to this file. */
        if (oldInfo.references.find(*i) == oldInfo.references.end())
            appendReferrer(*i, info.path, false);
    }

    assert(info.hash.type == htSHA256);

    string s = (format(
        "Hash: sha256:%1%\n"
        "References: %2%\n"
        "Deriver: %3%\n"
        "Registered-At: %4%\n")
        % printHash(info.hash) % refs % info.deriver %
        (oldInfo.registrationTime ? oldInfo.registrationTime : time(0))).str();

    /* Atomically rewrite the info file. */
    Path tmpFile = tmpFileForAtomicUpdate(infoFile);
    writeFile(tmpFile, s, doFsync);
    if (rename(tmpFile.c_str(), infoFile.c_str()) == -1)
        throw SysError(format("cannot rename `%1%' to `%2%'") % tmpFile % infoFile);
#endif
}


void LocalStore::registerFailedPath(const Path & path)
{
#if 0
    /* Write an empty file in the .../failed directory to denote the
       failure of the builder for `path'. */
    writeFile(failedFileFor(path), "");
#endif
}


bool LocalStore::hasPathFailed(const Path & path)
{
#if 0
    return pathExists(failedFileFor(path));
#endif
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


ValidPathInfo LocalStore::queryPathInfo(const Path & path, bool ignoreErrors)
{
#if 0
    ValidPathInfo res;
    res.path = path;

    assertStorePath(path);

    if (!isValidPath(path))
        throw Error(format("path `%1%' is not valid") % path);

    /* Read the info file. */
    Path infoFile = infoFileFor(path);
    if (!pathExists(infoFile))
        throw Error(format("path `%1%' is not valid") % path);
    string info = readFile(infoFile);

    /* Parse it. */
    Strings lines = tokenizeString(info, "\n");

    foreach (Strings::iterator, i, lines) {
        string::size_type p = i->find(':');
        if (p == string::npos) {
            if (!ignoreErrors)
                throw Error(format("corrupt line in `%1%': %2%") % infoFile % *i);
            continue; /* bad line */
        }
        string name(*i, 0, p);
        string value(*i, p + 2);
        if (name == "References") {
            Strings refs = tokenizeString(value, " ");
            res.references = PathSet(refs.begin(), refs.end());
        } else if (name == "Deriver") {
            res.deriver = value;
        } else if (name == "Hash") {
            try {
                res.hash = parseHashField(path, value);
            } catch (Error & e) {
                if (!ignoreErrors) throw;
                printMsg(lvlError, format("cannot parse hash field in `%1%': %2%") % infoFile % e.msg());
            }
        } else if (name == "Registered-At") {
            int n = 0;
            string2Int(value, n);
            res.registrationTime = n;
        }
    }

    return res;
#endif
}


bool LocalStore::isValidPath(const Path & path)
{
    stmtIsValidPath.reset();
    if (sqlite3_bind_text(stmtIsValidPath, 1, path.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
        throw SQLiteError(db, "binding argument");
    int res = sqlite3_step(stmtIsValidPath);
    if (res != SQLITE_DONE && res != SQLITE_ROW)
        throw SQLiteError(db, "querying path in database");
    return res == SQLITE_ROW;
}


PathSet LocalStore::queryValidPaths()
{
    PathSet paths;
    Strings entries = readDirectory(nixDBPath + "/info");
    foreach (Strings::iterator, i, entries)
        if (i->at(0) != '.') paths.insert(nixStore + "/" + *i);
    return paths;
}


void LocalStore::queryReferences(const Path & path,
    PathSet & references)
{
    ValidPathInfo info = queryPathInfo(path);
    references.insert(info.references.begin(), info.references.end());
}


bool LocalStore::queryReferrersInternal(const Path & path, PathSet & referrers)
{
#if 0
    bool allValid = true;
    
    if (!isValidPath(path))
        throw Error(format("path `%1%' is not valid") % path);

    /* No locking is necessary here: updates are only done by
       appending or by atomically replacing the file.  When appending,
       there is a possibility that we see a partial entry, but it will
       just be filtered out below (the partially written path will not
       be valid, so it will be ignored). */

    Path referrersFile = referrersFileFor(path);
    if (!pathExists(referrersFile)) return true;
    
    AutoCloseFD fd = open(referrersFile.c_str(), O_RDONLY);
    if (fd == -1) throw SysError(format("opening file `%1%'") % referrersFile);

    Paths refs = tokenizeString(readFile(fd), " ");

    foreach (Paths::iterator, i, refs)
        /* Referrers can be invalid (see registerValidPath() for the
           invariant), so we only return one if it is valid. */
        if (isStorePath(*i) && isValidPath(*i)) referrers.insert(*i); else allValid = false;

    return allValid;
#endif
}


void LocalStore::queryReferrers(const Path & path, PathSet & referrers)
{
    queryReferrersInternal(path, referrers);
}


Path LocalStore::queryDeriver(const Path & path)
{
    return queryPathInfo(path).deriver;
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


static void dfsVisit(std::map<Path, ValidPathInfo> & infos,
    const Path & path, PathSet & visited, Paths & sorted)
{
    if (visited.find(path) != visited.end()) return;
    visited.insert(path);

    ValidPathInfo & info(infos[path]);
    
    foreach (PathSet::iterator, i, info.references)
        if (infos.find(*i) != infos.end())
            dfsVisit(infos, *i, visited, sorted);

    sorted.push_back(path);
}


void LocalStore::registerValidPaths(const ValidPathInfos & infos)
{
    std::map<Path, ValidPathInfo> infosMap;
    
    /* Sort the paths topologically under the references relation, so
       that if path A is referenced by B, then A is registered before
       B. */
    foreach (ValidPathInfos::const_iterator, i, infos)
        infosMap[i->path] = *i;

    PathSet visited;
    Paths sorted;
    foreach (ValidPathInfos::const_iterator, i, infos)
        dfsVisit(infosMap, i->path, visited, sorted);

    foreach (Paths::iterator, i, sorted)
        registerValidPath(infosMap[*i]);
}


/* Invalidate a path.  The caller is responsible for checking that
   there are no referrers. */
void LocalStore::invalidatePath(const Path & path)
{
#if 0    
    debug(format("invalidating path `%1%'") % path);

    ValidPathInfo info;

    if (pathExists(infoFileFor(path))) {
        info = queryPathInfo(path);

        /* Remove the info file. */
        Path p = infoFileFor(path);
        if (unlink(p.c_str()) == -1)
            throw SysError(format("unlinking `%1%'") % p);
    }

    /* Remove the referrers file for `path'. */
    Path p = referrersFileFor(path);
    if (pathExists(p) && unlink(p.c_str()) == -1)
        throw SysError(format("unlinking `%1%'") % p);
#endif
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
            registerValidPath(dstPath,
                (recursive && hashAlgo == htSHA256) ? h :
                (recursive ? hashString(htSHA256, dump) : hashPath(htSHA256, dstPath)),
                PathSet(), "");
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
            
            registerValidPath(dstPath,
                hashPath(htSHA256, dstPath), references, "");
        }

        outputLock.setDeletion(true);
    }

    return dstPath;
}


struct HashAndWriteSink : Sink
{
    Sink & writeSink;
    HashSink hashSink;
    bool hashing;
    HashAndWriteSink(Sink & writeSink) : writeSink(writeSink), hashSink(htSHA256)
    {
        hashing = true;
    }
    virtual void operator ()
        (const unsigned char * data, unsigned int len)
    {
        writeSink(data, len);
        if (hashing) hashSink(data, len);
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

    writeInt(EXPORT_MAGIC, hashAndWriteSink);

    writeString(path, hashAndWriteSink);
    
    PathSet references;
    queryReferences(path, references);
    writeStringSet(references, hashAndWriteSink);

    Path deriver = queryDeriver(path);
    writeString(deriver, hashAndWriteSink);

    if (sign) {
        Hash hash = hashAndWriteSink.hashSink.finish();
        hashAndWriteSink.hashing = false;

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


Path LocalStore::importPath(bool requireSignature, Source & source)
{
    HashAndReadSource hashAndReadSource(source);
    
    /* We don't yet know what store path this archive contains (the
       store path follows the archive data proper), and besides, we
       don't know yet whether the signature is valid. */
    Path tmpDir = createTempDir(nixStore);
    AutoDelete delTmp(tmpDir); /* !!! could be GC'ed! */
    Path unpacked = tmpDir + "/unpacked";

    restorePath(unpacked, hashAndReadSource);

    unsigned int magic = readInt(hashAndReadSource);
    if (magic != EXPORT_MAGIC)
        throw Error("Nix archive cannot be imported; wrong format");

    Path dstPath = readStorePath(hashAndReadSource);

    PathSet references = readStorePaths(hashAndReadSource);

    Path deriver = readString(hashAndReadSource);
    if (deriver != "") assertStorePath(deriver);

    Hash hash = hashAndReadSource.hashSink.finish();
    hashAndReadSource.hashing = false;

    bool haveSignature = readInt(hashAndReadSource) == 1;

    if (requireSignature && !haveSignature)
        throw Error("imported archive lacks a signature");
    
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
            if (deriver != "" && !isValidPath(deriver)) deriver = "";
            registerValidPath(dstPath,
                hashPath(htSHA256, dstPath), references, deriver);
        }
        
        outputLock.setDeletion(true);
    }
    
    return dstPath;
}


void LocalStore::deleteFromStore(const Path & path, unsigned long long & bytesFreed,
    unsigned long long & blocksFreed)
{
#if 0
    bytesFreed = 0;

    assertStorePath(path);

    if (isValidPath(path)) {
        /* Acquire a lock on the referrers file to prevent new
           referrers to this path from appearing while we're deleting
           it. */
        PathLocks referrersLock(singleton<PathSet, Path>(referrersFileFor(path)));
        referrersLock.setDeletion(true);
        PathSet referrers; queryReferrers(path, referrers);
        referrers.erase(path); /* ignore self-references */
        if (!referrers.empty())
            throw PathInUse(format("cannot delete path `%1%' because it is in use by `%2%'")
                % path % showPaths(referrers));
        invalidatePath(path);
    }

    deletePathWrapped(path, bytesFreed, blocksFreed);
#endif
}


void LocalStore::verifyStore(bool checkContents)
{
#if 0
    /* Check whether all valid paths actually exist. */
    printMsg(lvlInfo, "checking path existence");

    PathSet validPaths2 = queryValidPaths(), validPaths;
    
    foreach (PathSet::iterator, i, validPaths2) {
        checkInterrupt();
        if (!isStorePath(*i)) {
            printMsg(lvlError, format("path `%1%' is not in the Nix store") % *i);
            invalidatePath(*i);
        } else if (!pathExists(*i)) {
            printMsg(lvlError, format("path `%1%' disappeared") % *i);
            invalidatePath(*i);
        } else {
            Path infoFile = infoFileFor(*i);
            struct stat st;
            if (lstat(infoFile.c_str(), &st))
                throw SysError(format("getting status of `%1%'") % infoFile);
            if (st.st_size == 0) {
                printMsg(lvlError, format("removing corrupt info file `%1%'") % infoFile);
                if (unlink(infoFile.c_str()) == -1)
                    throw SysError(format("unlinking `%1%'") % infoFile);
            }
            else validPaths.insert(*i);
        }
    }


    /* Check the store path meta-information. */
    printMsg(lvlInfo, "checking path meta-information");

    std::map<Path, PathSet> referrersCache;
    
    foreach (PathSet::iterator, i, validPaths) {
        bool update = false;
        ValidPathInfo info = queryPathInfo(*i, true);

        /* Check the references: each reference should be valid, and
           it should have a matching referrer. */
        foreach (PathSet::iterator, j, info.references) {
            if (validPaths.find(*j) == validPaths.end()) {
                printMsg(lvlError, format("incomplete closure: `%1%' needs missing `%2%'")
                    % *i % *j);
                /* nothing we can do about it... */
            } else {
                if (referrersCache.find(*j) == referrersCache.end())
                    queryReferrers(*j, referrersCache[*j]);
                if (referrersCache[*j].find(*i) == referrersCache[*j].end()) {
                    printMsg(lvlError, format("adding missing referrer mapping from `%1%' to `%2%'")
                        % *j % *i);
                    appendReferrer(*j, *i, true);
                }
            }
        }

        /* Check the deriver.  (Note that the deriver doesn't have to
           be a valid path.) */
        if (!info.deriver.empty() && !isStorePath(info.deriver)) {
            info.deriver = "";
            update = true;
        }

        /* Check the content hash (optionally - slow). */
        if (info.hash.hashSize == 0) {
            printMsg(lvlError, format("re-hashing `%1%'") % *i);
            info.hash = hashPath(htSHA256, *i);
            update = true;
        } else if (checkContents) {
            debug(format("checking contents of `%1%'") % *i);
            Hash current = hashPath(info.hash.type, *i);
            if (current != info.hash) {
                printMsg(lvlError, format("path `%1%' was modified! "
                        "expected hash `%2%', got `%3%'")
                    % *i % printHash(info.hash) % printHash(current));
            }
        }

        if (update) registerValidPath(info);
    }

    referrersCache.clear();
    

    /* Check the referrers. */
    printMsg(lvlInfo, "checking referrers");

    std::map<Path, PathSet> referencesCache;
    
    Strings entries = readDirectory(nixDBPath + "/referrer");
    foreach (Strings::iterator, i, entries) {
        Path from = nixStore + "/" + *i;
        
        if (validPaths.find(from) == validPaths.end()) {
            /* !!! This removes lock files as well.  Need to check
               whether that's okay. */
            printMsg(lvlError, format("removing referrers file for invalid `%1%'") % from);
            Path p = referrersFileFor(from);
            if (unlink(p.c_str()) == -1)
                throw SysError(format("unlinking `%1%'") % p);
            continue;
        }

        PathSet referrers;
        bool allValid = queryReferrersInternal(from, referrers);
        bool update = false;

        if (!allValid) {
            printMsg(lvlError, format("removing some stale referrers for `%1%'") % from);
            update = true;
        }

        /* Each referrer should have a matching reference. */
        PathSet referrersNew;
        foreach (PathSet::iterator, j, referrers) {
            if (referencesCache.find(*j) == referencesCache.end())
                queryReferences(*j, referencesCache[*j]);
            if (referencesCache[*j].find(from) == referencesCache[*j].end()) {
                printMsg(lvlError, format("removing unexpected referrer mapping from `%1%' to `%2%'")
                    % from % *j);
                update = true;
            } else referrersNew.insert(*j);
        }

        if (update) rewriteReferrers(from, false, referrersNew);
    }
#endif
}


/* Functions for upgrading from the pre-SQLite database. */

static Path infoFileFor(const Path & path)
{
    string baseName = baseNameOf(path);
    return (format("%1%/info/%2%") % nixDBPath % baseName).str();
}


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
    Path infoFile = infoFileFor(path);
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
    if (!lockFile(globalLock, ltWrite, false)) {
        printMsg(lvlError, "waiting for exclusive access to the Nix store...");
        lockFile(globalLock, ltWrite, true);
    }

    printMsg(lvlError, "upgrading Nix store to new schema (this may take a while)...");

    initSchema();

    PathSet validPaths = queryValidPathsOld();

    SQLiteTxn txn(db);
    
    std::map<Path, sqlite3_int64> pathToId;
    
    foreach (PathSet::iterator, i, validPaths) {
        ValidPathInfo info = queryPathInfoOld(*i);
        
        stmtRegisterValidPath.reset();
        if (sqlite3_bind_text(stmtRegisterValidPath, 1, i->c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
            throw SQLiteError(db, "binding argument 1");
        string h = "sha256:" + printHash(info.hash);
        if (sqlite3_bind_text(stmtRegisterValidPath, 2, h.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
            throw SQLiteError(db, "binding argument 2");
        if (sqlite3_bind_int(stmtRegisterValidPath, 3, info.registrationTime) != SQLITE_OK)
            throw SQLiteError(db, "binding argument 3");
        if (info.deriver != "") {
            if (sqlite3_bind_text(stmtRegisterValidPath, 4, info.deriver.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
                throw SQLiteError(db, "binding argument 4");
        } else {
            if (sqlite3_bind_null(stmtRegisterValidPath, 4) != SQLITE_OK)
                throw SQLiteError(db, "binding argument 4");
        }
        if (sqlite3_step(stmtRegisterValidPath) != SQLITE_DONE)
            throw SQLiteError(db, "registering valid path in database");

        pathToId[*i] = sqlite3_last_insert_rowid(db);

        std::cerr << ".";
    }

    std::cerr << "|";
    
    foreach (PathSet::iterator, i, validPaths) {
        ValidPathInfo info = queryPathInfoOld(*i);
        
        foreach (PathSet::iterator, j, info.references) {
            stmtAddReference.reset();
            if (sqlite3_bind_int(stmtAddReference, 1, pathToId[*i]) != SQLITE_OK)
                throw SQLiteError(db, "binding argument 1");
            if (pathToId.find(*j) == pathToId.end())
                throw Error(format("path `%1%' referenced by `%2%' is invalid") % *j % *i);
            if (sqlite3_bind_int(stmtAddReference, 2, pathToId[*j]) != SQLITE_OK)
                throw SQLiteError(db, "binding argument 2");
            if (sqlite3_step(stmtAddReference) != SQLITE_DONE)
                throw SQLiteError(db, "adding reference to database");
        }

        std::cerr << ".";
    }

    std::cerr << "\n";

    txn.commit();

    writeFile(schemaPath, (format("%1%") % nixSchemaVersion).str());

    lockFile(globalLock, ltRead, true);
}


}
