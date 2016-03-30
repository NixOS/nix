#include "config.h"
#include "local-store.hh"
#include "globals.hh"
#include "archive.hh"
#include "pathlocks.hh"
#include "worker-protocol.hh"
#include "derivations.hh"
#include "affinity.hh"

#include <iostream>
#include <algorithm>
#include <cstring>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <utime.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <grp.h>

#if __linux__
#include <sched.h>
#include <sys/statvfs.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#endif

#include <sqlite3.h>


namespace nix {


void checkStoreNotSymlink()
{
    if (getEnv("NIX_IGNORE_SYMLINK_STORE") == "1") return;
    Path path = settings.nixStore;
    struct stat st;
    while (path != "/") {
        if (lstat(path.c_str(), &st))
            throw SysError(format("getting status of ‘%1%’") % path);
        if (S_ISLNK(st.st_mode))
            throw Error(format(
                "the path ‘%1%’ is a symlink; "
                "this is not allowed for the Nix store and its parent directories")
                % path);
        path = dirOf(path);
    }
}


LocalStore::LocalStore()
    : reservedPath(settings.nixDBPath + "/reserved")
    , didSetSubstituterEnv(false)
{
    schemaPath = settings.nixDBPath + "/schema";

    if (settings.readOnlyMode) {
        openDB(false);
        return;
    }

    /* Create missing state directories if they don't already exist. */
    createDirs(settings.nixStore);
    makeStoreWritable();
    createDirs(linksDir = settings.nixStore + "/.links");
    Path profilesDir = settings.nixStateDir + "/profiles";
    createDirs(profilesDir);
    createDirs(settings.nixStateDir + "/temproots");
    createDirs(settings.nixDBPath);
    Path gcRootsDir = settings.nixStateDir + "/gcroots";
    if (!pathExists(gcRootsDir)) {
        createDirs(gcRootsDir);
        createSymlink(profilesDir, gcRootsDir + "/profiles");
    }

    /* Optionally, create directories and set permissions for a
       multi-user install. */
    if (getuid() == 0 && settings.buildUsersGroup != "") {

        Path perUserDir = profilesDir + "/per-user";
        createDirs(perUserDir);
        if (chmod(perUserDir.c_str(), 01777) == -1)
            throw SysError(format("could not set permissions on ‘%1%’ to 1777") % perUserDir);

        mode_t perm = 01775;

        struct group * gr = getgrnam(settings.buildUsersGroup.c_str());
        if (!gr)
            printMsg(lvlError, format("warning: the group ‘%1%’ specified in ‘build-users-group’ does not exist")
                % settings.buildUsersGroup);
        else {
            struct stat st;
            if (stat(settings.nixStore.c_str(), &st))
                throw SysError(format("getting attributes of path ‘%1%’") % settings.nixStore);

            if (st.st_uid != 0 || st.st_gid != gr->gr_gid || (st.st_mode & ~S_IFMT) != perm) {
                if (chown(settings.nixStore.c_str(), 0, gr->gr_gid) == -1)
                    throw SysError(format("changing ownership of path ‘%1%’") % settings.nixStore);
                if (chmod(settings.nixStore.c_str(), perm) == -1)
                    throw SysError(format("changing permissions on path ‘%1%’") % settings.nixStore);
            }
        }
    }

    checkStoreNotSymlink();

    /* We can't open a SQLite database if the disk is full.  Since
       this prevents the garbage collector from running when it's most
       needed, we reserve some dummy space that we can free just
       before doing a garbage collection. */
    try {
        struct stat st;
        if (stat(reservedPath.c_str(), &st) == -1 ||
            st.st_size != settings.reservedSize)
        {
            AutoCloseFD fd = open(reservedPath.c_str(), O_WRONLY | O_CREAT, 0600);
            int res = -1;
#if HAVE_POSIX_FALLOCATE
            res = posix_fallocate(fd, 0, settings.reservedSize);
#endif
            if (res == -1) {
                writeFull(fd, string(settings.reservedSize, 'X'));
                ftruncate(fd, settings.reservedSize);
            }
        }
    } catch (SysError & e) { /* don't care about errors */
    }

    /* Acquire the big fat lock in shared mode to make sure that no
       schema upgrade is in progress. */
    try {
        Path globalLockPath = settings.nixDBPath + "/big-lock";
        globalLock = openLockFile(globalLockPath.c_str(), true);
    } catch (SysError & e) {
        if (e.errNo != EACCES) throw;
        settings.readOnlyMode = true;
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

        if (curSchema < 6)
            throw Error(
                "Your Nix store has a database in flat file format,\n"
                "which is no longer supported. To convert to the new format,\n"
                "please upgrade Nix to version 1.11 first.");

        if (!lockFile(globalLock, ltWrite, false)) {
            printMsg(lvlError, "waiting for exclusive access to the Nix store...");
            lockFile(globalLock, ltWrite, true);
        }

        /* Get the schema version again, because another process may
           have performed the upgrade already. */
        curSchema = getSchema();

        if (curSchema < 7) { upgradeStore7(); openDB(true); }

        writeFile(schemaPath, (format("%1%") % nixSchemaVersion).str());

        lockFile(globalLock, ltRead, true);
    }

    else openDB(false);
}


LocalStore::~LocalStore()
{
    try {
        for (auto & i : runningSubstituters) {
            if (i.second.disabled) continue;
            i.second.to.close();
            i.second.from.close();
            i.second.error.close();
            if (i.second.pid != -1)
                i.second.pid.wait(true);
        }
    } catch (...) {
        ignoreException();
    }

    try {
        if (fdTempRoots != -1) {
            fdTempRoots.close();
            unlink(fnTempRoots.c_str());
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
            throw Error(format("‘%1%’ is corrupt") % schemaPath);
    }
    return curSchema;
}


bool LocalStore::haveWriteAccess()
{
    return access(settings.nixDBPath.c_str(), R_OK | W_OK) == 0;
}


void LocalStore::openDB(bool create)
{
    if (!haveWriteAccess())
        throw SysError(format("Nix database directory ‘%1%’ is not writable") % settings.nixDBPath);

    /* Open the Nix database. */
    string dbPath = settings.nixDBPath + "/db.sqlite";
    if (sqlite3_open_v2(dbPath.c_str(), &db.db,
            SQLITE_OPEN_READWRITE | (create ? SQLITE_OPEN_CREATE : 0), 0) != SQLITE_OK)
        throw Error(format("cannot open Nix database ‘%1%’") % dbPath);

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
    string syncMode = settings.fsyncMetadata ? "normal" : "off";
    if (sqlite3_exec(db, ("pragma synchronous = " + syncMode + ";").c_str(), 0, 0, 0) != SQLITE_OK)
        throwSQLiteError(db, "setting synchronous mode");

    /* Set the SQLite journal mode.  WAL mode is fastest, so it's the
       default. */
    string mode = settings.useSQLiteWAL ? "wal" : "truncate";
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

    /* Increase the auto-checkpoint interval to 40000 pages.  This
       seems enough to ensure that instantiating the NixOS system
       derivation is done in a single fsync(). */
    if (mode == "wal" && sqlite3_exec(db, "pragma wal_autocheckpoint = 40000;", 0, 0, 0) != SQLITE_OK)
        throwSQLiteError(db, "setting autocheckpoint interval");

    /* Initialise the database schema, if necessary. */
    if (create) {
        const char * schema =
#include "schema.sql.hh"
            ;
        if (sqlite3_exec(db, (const char *) schema, 0, 0, 0) != SQLITE_OK)
            throwSQLiteError(db, "initialising database schema");
    }

    /* Prepare SQL statements. */
    stmtRegisterValidPath.create(db,
        "insert into ValidPaths (path, hash, registrationTime, deriver, narSize) values (?, ?, ?, ?, ?);");
    stmtUpdatePathInfo.create(db,
        "update ValidPaths set narSize = ?, hash = ? where path = ?;");
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
        "insert or ignore into FailedPaths (path, time) values (?, ?);");
    stmtHasPathFailed.create(db,
        "select time from FailedPaths where path = ?;");
    stmtQueryFailedPaths.create(db,
        "select path from FailedPaths;");
    // If the path is a derivation, then clear its outputs.
    stmtClearFailedPath.create(db,
        "delete from FailedPaths where ?1 = '*' or path = ?1 "
        "or path in (select d.path from DerivationOutputs d join ValidPaths v on d.drv = v.id where v.path = ?1);");
    stmtAddDerivationOutput.create(db,
        "insert or replace into DerivationOutputs (drv, id, path) values (?, ?, ?);");
    stmtQueryValidDerivers.create(db,
        "select v.id, v.path from DerivationOutputs d join ValidPaths v on d.drv = v.id where d.path = ?;");
    stmtQueryDerivationOutputs.create(db,
        "select id, path from DerivationOutputs where drv = ?;");
    // Use "path >= ?" with limit 1 rather than "path like '?%'" to
    // ensure efficient lookup.
    stmtQueryPathFromHashPart.create(db,
        "select path from ValidPaths where path >= ? limit 1;");
    stmtQueryValidPaths.create(db, "select path from ValidPaths");
}


/* To improve purity, users may want to make the Nix store a read-only
   bind mount.  So make the Nix store writable for this process. */
void LocalStore::makeStoreWritable()
{
#if __linux__
    if (getuid() != 0) return;
    /* Check if /nix/store is on a read-only mount. */
    struct statvfs stat;
    if (statvfs(settings.nixStore.c_str(), &stat) != 0)
        throw SysError("getting info about the Nix store mount point");

    if (stat.f_flag & ST_RDONLY) {
        if (unshare(CLONE_NEWNS) == -1)
            throw SysError("setting up a private mount namespace");

        if (mount(0, settings.nixStore.c_str(), "none", MS_REMOUNT | MS_BIND, 0) == -1)
            throw SysError(format("remounting %1% writable") % settings.nixStore);
    }
#endif
}


const time_t mtimeStore = 1; /* 1 second into the epoch */


static void canonicaliseTimestampAndPermissions(const Path & path, const struct stat & st)
{
    if (!S_ISLNK(st.st_mode)) {

        /* Mask out all type related bits. */
        mode_t mode = st.st_mode & ~S_IFMT;

        if (mode != 0444 && mode != 0555) {
            mode = (st.st_mode & S_IFMT)
                 | 0444
                 | (st.st_mode & S_IXUSR ? 0111 : 0);
            if (chmod(path.c_str(), mode) == -1)
                throw SysError(format("changing mode of ‘%1%’ to %2$o") % path % mode);
        }

    }

    if (st.st_mtime != mtimeStore) {
        struct timeval times[2];
        times[0].tv_sec = st.st_atime;
        times[0].tv_usec = 0;
        times[1].tv_sec = mtimeStore;
        times[1].tv_usec = 0;
#if HAVE_LUTIMES
        if (lutimes(path.c_str(), times) == -1)
            if (errno != ENOSYS ||
                (!S_ISLNK(st.st_mode) && utimes(path.c_str(), times) == -1))
#else
        if (!S_ISLNK(st.st_mode) && utimes(path.c_str(), times) == -1)
#endif
            throw SysError(format("changing modification time of ‘%1%’") % path);
    }
}


void canonicaliseTimestampAndPermissions(const Path & path)
{
    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError(format("getting attributes of path ‘%1%’") % path);
    canonicaliseTimestampAndPermissions(path, st);
}


static void canonicalisePathMetaData_(const Path & path, uid_t fromUid, InodesSeen & inodesSeen)
{
    checkInterrupt();

    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError(format("getting attributes of path ‘%1%’") % path);

    /* Really make sure that the path is of a supported type. */
    if (!(S_ISREG(st.st_mode) || S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)))
        throw Error(format("file ‘%1%’ has an unsupported type") % path);

    /* Fail if the file is not owned by the build user.  This prevents
       us from messing up the ownership/permissions of files
       hard-linked into the output (e.g. "ln /etc/shadow $out/foo").
       However, ignore files that we chown'ed ourselves previously to
       ensure that we don't fail on hard links within the same build
       (i.e. "touch $out/foo; ln $out/foo $out/bar"). */
    if (fromUid != (uid_t) -1 && st.st_uid != fromUid) {
        assert(!S_ISDIR(st.st_mode));
        if (inodesSeen.find(Inode(st.st_dev, st.st_ino)) == inodesSeen.end())
            throw BuildError(format("invalid ownership on file ‘%1%’") % path);
        mode_t mode = st.st_mode & ~S_IFMT;
        assert(S_ISLNK(st.st_mode) || (st.st_uid == geteuid() && (mode == 0444 || mode == 0555) && st.st_mtime == mtimeStore));
        return;
    }

    inodesSeen.insert(Inode(st.st_dev, st.st_ino));

    canonicaliseTimestampAndPermissions(path, st);

    /* Change ownership to the current uid.  If it's a symlink, use
       lchown if available, otherwise don't bother.  Wrong ownership
       of a symlink doesn't matter, since the owning user can't change
       the symlink and can't delete it because the directory is not
       writable.  The only exception is top-level paths in the Nix
       store (since that directory is group-writable for the Nix build
       users group); we check for this case below. */
    if (st.st_uid != geteuid()) {
#if HAVE_LCHOWN
        if (lchown(path.c_str(), geteuid(), getegid()) == -1)
#else
        if (!S_ISLNK(st.st_mode) &&
            chown(path.c_str(), geteuid(), getegid()) == -1)
#endif
            throw SysError(format("changing owner of ‘%1%’ to %2%")
                % path % geteuid());
    }

    if (S_ISDIR(st.st_mode)) {
        DirEntries entries = readDirectory(path);
        for (auto & i : entries)
            canonicalisePathMetaData_(path + "/" + i.name, fromUid, inodesSeen);
    }
}


void canonicalisePathMetaData(const Path & path, uid_t fromUid, InodesSeen & inodesSeen)
{
    canonicalisePathMetaData_(path, fromUid, inodesSeen);

    /* On platforms that don't have lchown(), the top-level path can't
       be a symlink, since we can't change its ownership. */
    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError(format("getting attributes of path ‘%1%’") % path);

    if (st.st_uid != geteuid()) {
        assert(S_ISLNK(st.st_mode));
        throw Error(format("wrong ownership of top-level store path ‘%1%’") % path);
    }
}


void canonicalisePathMetaData(const Path & path, uid_t fromUid)
{
    InodesSeen inodesSeen;
    canonicalisePathMetaData(path, fromUid, inodesSeen);
}


void LocalStore::checkDerivationOutputs(const Path & drvPath, const Derivation & drv)
{
    string drvName = storePathToName(drvPath);
    assert(isDerivation(drvName));
    drvName = string(drvName, 0, drvName.size() - drvExtension.size());

    if (drv.isFixedOutput()) {
        DerivationOutputs::const_iterator out = drv.outputs.find("out");
        if (out == drv.outputs.end())
            throw Error(format("derivation ‘%1%’ does not have an output named ‘out’") % drvPath);

        bool recursive; HashType ht; Hash h;
        out->second.parseHashInfo(recursive, ht, h);
        Path outPath = makeFixedOutputPath(recursive, ht, h, drvName);

        StringPairs::const_iterator j = drv.env.find("out");
        if (out->second.path != outPath || j == drv.env.end() || j->second != outPath)
            throw Error(format("derivation ‘%1%’ has incorrect output ‘%2%’, should be ‘%3%’")
                % drvPath % out->second.path % outPath);
    }

    else {
        Derivation drvCopy(drv);
        for (auto & i : drvCopy.outputs) {
            i.second.path = "";
            drvCopy.env[i.first] = "";
        }

        Hash h = hashDerivationModulo(*this, drvCopy);

        for (auto & i : drv.outputs) {
            Path outPath = makeOutputPath(i.first, h, drvName);
            StringPairs::const_iterator j = drv.env.find(i.first);
            if (i.second.path != outPath || j == drv.env.end() || j->second != outPath)
                throw Error(format("derivation ‘%1%’ has incorrect output ‘%2%’, should be ‘%3%’")
                    % drvPath % i.second.path % outPath);
        }
    }
}


uint64_t LocalStore::addValidPath(const ValidPathInfo & info, bool checkOutputs)
{
    stmtRegisterValidPath.use()
        (info.path)
        ("sha256:" + printHash(info.narHash))
        (info.registrationTime == 0 ? time(0) : info.registrationTime)
        (info.deriver, info.deriver != "")
        (info.narSize, info.narSize != 0)
        .exec();
    uint64_t id = sqlite3_last_insert_rowid(db);

    /* If this is a derivation, then store the derivation outputs in
       the database.  This is useful for the garbage collector: it can
       efficiently query whether a path is an output of some
       derivation. */
    if (isDerivation(info.path)) {
        Derivation drv = readDerivation(info.path);

        /* Verify that the output paths in the derivation are correct
           (i.e., follow the scheme for computing output paths from
           derivations).  Note that if this throws an error, then the
           DB transaction is rolled back, so the path validity
           registration above is undone. */
        if (checkOutputs) checkDerivationOutputs(info.path, drv);

        for (auto & i : drv.outputs) {
            stmtAddDerivationOutput.use()
                (id)
                (i.first)
                (i.second.path)
                .exec();
        }
    }

    return id;
}


void LocalStore::addReference(uint64_t referrer, uint64_t reference)
{
    stmtAddReference.use()(referrer)(reference).exec();
}


void LocalStore::registerFailedPath(const Path & path)
{
    retrySQLite<void>([&]() {
        stmtRegisterFailedPath.use()(path)(time(0)).step();
    });
}


bool LocalStore::hasPathFailed(const Path & path)
{
    return retrySQLite<bool>([&]() {
        return stmtHasPathFailed.use()(path).next();
    });
}


PathSet LocalStore::queryFailedPaths()
{
    return retrySQLite<PathSet>([&]() {
        auto useQueryFailedPaths(stmtQueryFailedPaths.use());

        PathSet res;
        while (useQueryFailedPaths.next())
            res.insert(useQueryFailedPaths.getStr(0));

        return res;
    });
}


void LocalStore::clearFailedPaths(const PathSet & paths)
{
    retrySQLite<void>([&]() {
        SQLiteTxn txn(db);

        for (auto & path : paths)
            stmtClearFailedPath.use()(path).exec();

        txn.commit();
    });
}


Hash parseHashField(const Path & path, const string & s)
{
    string::size_type colon = s.find(':');
    if (colon == string::npos)
        throw Error(format("corrupt hash ‘%1%’ in valid-path entry for ‘%2%’")
            % s % path);
    HashType ht = parseHashType(string(s, 0, colon));
    if (ht == htUnknown)
        throw Error(format("unknown hash type ‘%1%’ in valid-path entry for ‘%2%’")
            % string(s, 0, colon) % path);
    return parseHash(ht, string(s, colon + 1));
}


ValidPathInfo LocalStore::queryPathInfo(const Path & path)
{
    ValidPathInfo info;
    info.path = path;

    assertStorePath(path);

    return retrySQLite<ValidPathInfo>([&]() {

        /* Get the path info. */
        auto useQueryPathInfo(stmtQueryPathInfo.use()(path));

        if (!useQueryPathInfo.next())
            throw Error(format("path ‘%1%’ is not valid") % path);

        info.id = useQueryPathInfo.getInt(0);

        info.narHash = parseHashField(path, useQueryPathInfo.getStr(1));

        info.registrationTime = useQueryPathInfo.getInt(2);

        auto s = (const char *) sqlite3_column_text(stmtQueryPathInfo, 3);
        if (s) info.deriver = s;

        /* Note that narSize = NULL yields 0. */
        info.narSize = useQueryPathInfo.getInt(4);

        /* Get the references. */
        auto useQueryReferences(stmtQueryReferences.use()(info.id));

        while (useQueryReferences.next())
            info.references.insert(useQueryReferences.getStr(0));

        return info;
    });
}


/* Update path info in the database.  Currently only updates the
   narSize field. */
void LocalStore::updatePathInfo(const ValidPathInfo & info)
{
    stmtUpdatePathInfo.use()
        (info.narSize, info.narSize != 0)
        ("sha256:" + printHash(info.narHash))
        (info.path)
        .exec();
}


uint64_t LocalStore::queryValidPathId(const Path & path)
{
    auto use(stmtQueryPathInfo.use()(path));
    if (!use.next())
        throw Error(format("path ‘%1%’ is not valid") % path);
    return use.getInt(0);
}


bool LocalStore::isValidPath_(const Path & path)
{
    return stmtQueryPathInfo.use()(path).next();
}


bool LocalStore::isValidPath(const Path & path)
{
    return retrySQLite<bool>([&]() {
        return isValidPath_(path);
    });
}


PathSet LocalStore::queryValidPaths(const PathSet & paths)
{
    return retrySQLite<PathSet>([&]() {
        PathSet res;
        for (auto & i : paths)
            if (isValidPath_(i)) res.insert(i);
        return res;
    });
}


PathSet LocalStore::queryAllValidPaths()
{
    return retrySQLite<PathSet>([&]() {
        auto use(stmtQueryValidPaths.use());
        PathSet res;
        while (use.next()) res.insert(use.getStr(0));
        return res;
    });
}


void LocalStore::queryReferrers_(const Path & path, PathSet & referrers)
{
    auto useQueryReferrers(stmtQueryReferrers.use()(path));

    while (useQueryReferrers.next())
        referrers.insert(useQueryReferrers.getStr(0));
}


void LocalStore::queryReferrers(const Path & path, PathSet & referrers)
{
    assertStorePath(path);
    return retrySQLite<void>([&]() {
        queryReferrers_(path, referrers);
    });
}


Path LocalStore::queryDeriver(const Path & path)
{
    return queryPathInfo(path).deriver;
}


PathSet LocalStore::queryValidDerivers(const Path & path)
{
    assertStorePath(path);

    return retrySQLite<PathSet>([&]() {
        auto useQueryValidDerivers(stmtQueryValidDerivers.use()(path));

        PathSet derivers;
        while (useQueryValidDerivers.next())
            derivers.insert(useQueryValidDerivers.getStr(1));

        return derivers;
    });
}


PathSet LocalStore::queryDerivationOutputs(const Path & path)
{
    return retrySQLite<PathSet>([&]() {
        auto useQueryDerivationOutputs(stmtQueryDerivationOutputs.use()(queryValidPathId(path)));

        PathSet outputs;
        while (useQueryDerivationOutputs.next())
            outputs.insert(useQueryDerivationOutputs.getStr(1));

        return outputs;
    });
}


StringSet LocalStore::queryDerivationOutputNames(const Path & path)
{
    return retrySQLite<StringSet>([&]() {
        auto useQueryDerivationOutputs(stmtQueryDerivationOutputs.use()(queryValidPathId(path)));

        StringSet outputNames;
        while (useQueryDerivationOutputs.next())
            outputNames.insert(useQueryDerivationOutputs.getStr(0));

        return outputNames;
    });
}


Path LocalStore::queryPathFromHashPart(const string & hashPart)
{
    if (hashPart.size() != storePathHashLen) throw Error("invalid hash part");

    Path prefix = settings.nixStore + "/" + hashPart;

    return retrySQLite<Path>([&]() {
        auto useQueryPathFromHashPart(stmtQueryPathFromHashPart.use()(prefix));

        if (!useQueryPathFromHashPart.next()) return "";

        const char * s = (const char *) sqlite3_column_text(stmtQueryPathFromHashPart, 0);
        return s && prefix.compare(0, prefix.size(), s, prefix.size()) == 0 ? s : "";
    });
}


void LocalStore::setSubstituterEnv()
{
    if (didSetSubstituterEnv) return;

    /* Pass configuration options (including those overridden with
       --option) to substituters. */
    setenv("_NIX_OPTIONS", settings.pack().c_str(), 1);

    didSetSubstituterEnv = true;
}


void LocalStore::startSubstituter(const Path & substituter, RunningSubstituter & run)
{
    if (run.disabled || run.pid != -1) return;

    debug(format("starting substituter program ‘%1%’") % substituter);

    Pipe toPipe, fromPipe, errorPipe;

    toPipe.create();
    fromPipe.create();
    errorPipe.create();

    setSubstituterEnv();

    run.pid = startProcess([&]() {
        if (dup2(toPipe.readSide, STDIN_FILENO) == -1)
            throw SysError("dupping stdin");
        if (dup2(fromPipe.writeSide, STDOUT_FILENO) == -1)
            throw SysError("dupping stdout");
        if (dup2(errorPipe.writeSide, STDERR_FILENO) == -1)
            throw SysError("dupping stderr");
        execl(substituter.c_str(), substituter.c_str(), "--query", NULL);
        throw SysError(format("executing ‘%1%’") % substituter);
    });

    run.program = baseNameOf(substituter);
    run.to = toPipe.writeSide.borrow();
    run.from = run.fromBuf.fd = fromPipe.readSide.borrow();
    run.error = errorPipe.readSide.borrow();

    toPipe.readSide.close();
    fromPipe.writeSide.close();
    errorPipe.writeSide.close();

    /* The substituter may exit right away if it's disabled in any way
       (e.g. copy-from-other-stores.pl will exit if no other stores
       are configured). */
    try {
        getLineFromSubstituter(run);
    } catch (EndOfFile & e) {
        run.to.close();
        run.from.close();
        run.error.close();
        run.disabled = true;
        if (run.pid.wait(true) != 0) throw;
    }
}


/* Read a line from the substituter's stdout, while also processing
   its stderr. */
string LocalStore::getLineFromSubstituter(RunningSubstituter & run)
{
    string res, err;

    /* We might have stdout data left over from the last time. */
    if (run.fromBuf.hasData()) goto haveData;

    while (1) {
        checkInterrupt();

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(run.from, &fds);
        FD_SET(run.error, &fds);

        /* Wait for data to appear on the substituter's stdout or
           stderr. */
        if (select(run.from > run.error ? run.from + 1 : run.error + 1, &fds, 0, 0, 0) == -1) {
            if (errno == EINTR) continue;
            throw SysError("waiting for input from the substituter");
        }

        /* Completely drain stderr before dealing with stdout. */
        if (FD_ISSET(run.error, &fds)) {
            char buf[4096];
            ssize_t n = read(run.error, (unsigned char *) buf, sizeof(buf));
            if (n == -1) {
                if (errno == EINTR) continue;
                throw SysError("reading from substituter's stderr");
            }
            if (n == 0) throw EndOfFile(format("substituter ‘%1%’ died unexpectedly") % run.program);
            err.append(buf, n);
            string::size_type p;
            while ((p = err.find('\n')) != string::npos) {
                printMsg(lvlError, run.program + ": " + string(err, 0, p));
                err = string(err, p + 1);
            }
        }

        /* Read from stdout until we get a newline or the buffer is empty. */
        else if (run.fromBuf.hasData() || FD_ISSET(run.from, &fds)) {
        haveData:
            do {
                unsigned char c;
                run.fromBuf(&c, 1);
                if (c == '\n') {
                    if (!err.empty()) printMsg(lvlError, run.program + ": " + err);
                    return res;
                }
                res += c;
            } while (run.fromBuf.hasData());
        }
    }
}


template<class T> T LocalStore::getIntLineFromSubstituter(RunningSubstituter & run)
{
    string s = getLineFromSubstituter(run);
    T res;
    if (!string2Int(s, res)) throw Error("integer expected from stream");
    return res;
}


PathSet LocalStore::querySubstitutablePaths(const PathSet & paths)
{
    PathSet res;
    for (auto & i : settings.substituters) {
        if (res.size() == paths.size()) break;
        RunningSubstituter & run(runningSubstituters[i]);
        startSubstituter(i, run);
        if (run.disabled) continue;
        string s = "have ";
        for (auto & j : paths)
            if (res.find(j) == res.end()) { s += j; s += " "; }
        writeLine(run.to, s);
        while (true) {
            /* FIXME: we only read stderr when an error occurs, so
               substituters should only write (short) messages to
               stderr when they fail.  I.e. they shouldn't write debug
               output. */
            Path path = getLineFromSubstituter(run);
            if (path == "") break;
            res.insert(path);
        }
    }
    return res;
}


void LocalStore::querySubstitutablePathInfos(const Path & substituter,
    PathSet & paths, SubstitutablePathInfos & infos)
{
    RunningSubstituter & run(runningSubstituters[substituter]);
    startSubstituter(substituter, run);
    if (run.disabled) return;

    string s = "info ";
    for (auto & i : paths)
        if (infos.find(i) == infos.end()) { s += i; s += " "; }
    writeLine(run.to, s);

    while (true) {
        Path path = getLineFromSubstituter(run);
        if (path == "") break;
        if (paths.find(path) == paths.end())
            throw Error(format("got unexpected path ‘%1%’ from substituter") % path);
        paths.erase(path);
        SubstitutablePathInfo & info(infos[path]);
        info.deriver = getLineFromSubstituter(run);
        if (info.deriver != "") assertStorePath(info.deriver);
        int nrRefs = getIntLineFromSubstituter<int>(run);
        while (nrRefs--) {
            Path p = getLineFromSubstituter(run);
            assertStorePath(p);
            info.references.insert(p);
        }
        info.downloadSize = getIntLineFromSubstituter<long long>(run);
        info.narSize = getIntLineFromSubstituter<long long>(run);
    }
}


void LocalStore::querySubstitutablePathInfos(const PathSet & paths,
    SubstitutablePathInfos & infos)
{
    PathSet todo = paths;
    for (auto & i : settings.substituters) {
        if (todo.empty()) break;
        querySubstitutablePathInfos(i, todo, infos);
    }
}


Hash LocalStore::queryPathHash(const Path & path)
{
    return queryPathInfo(path).narHash;
}


void LocalStore::registerValidPath(const ValidPathInfo & info)
{
    ValidPathInfos infos;
    infos.push_back(info);
    registerValidPaths(infos);
}


void LocalStore::registerValidPaths(const ValidPathInfos & infos)
{
    /* SQLite will fsync by default, but the new valid paths may not be fsync-ed.
     * So some may want to fsync them before registering the validity, at the
     * expense of some speed of the path registering operation. */
    if (settings.syncBeforeRegistering) sync();

    return retrySQLite<void>([&]() {
        SQLiteTxn txn(db);
        PathSet paths;

        for (auto & i : infos) {
            assert(i.narHash.type == htSHA256);
            if (isValidPath_(i.path))
                updatePathInfo(i);
            else
                addValidPath(i, false);
            paths.insert(i.path);
        }

        for (auto & i : infos) {
            auto referrer = queryValidPathId(i.path);
            for (auto & j : i.references)
                addReference(referrer, queryValidPathId(j));
        }

        /* Check that the derivation outputs are correct.  We can't do
           this in addValidPath() above, because the references might
           not be valid yet. */
        for (auto & i : infos)
            if (isDerivation(i.path)) {
                // FIXME: inefficient; we already loaded the
                // derivation in addValidPath().
                Derivation drv = readDerivation(i.path);
                checkDerivationOutputs(i.path, drv);
            }

        /* Do a topological sort of the paths.  This will throw an
           error if a cycle is detected and roll back the
           transaction.  Cycles can only occur when a derivation
           has multiple outputs. */
        topoSortPaths(paths);

        txn.commit();
    });
}


/* Invalidate a path.  The caller is responsible for checking that
   there are no referrers. */
void LocalStore::invalidatePath(const Path & path)
{
    debug(format("invalidating path ‘%1%’") % path);

    drvHashes.erase(path);

    stmtInvalidatePath.use()(path).exec();

    /* Note that the foreign key constraints on the Refs table take
       care of deleting the references entries for `path'. */
}


Path LocalStore::addToStoreFromDump(const string & dump, const string & name,
    bool recursive, HashType hashAlgo, bool repair)
{
    Hash h = hashString(hashAlgo, dump);

    Path dstPath = makeFixedOutputPath(recursive, hashAlgo, h, name);

    addTempRoot(dstPath);

    if (repair || !isValidPath(dstPath)) {

        /* The first check above is an optimisation to prevent
           unnecessary lock acquisition. */

        PathLocks outputLock(singleton<PathSet, Path>(dstPath));

        if (repair || !isValidPath(dstPath)) {

            deletePath(dstPath);

            if (recursive) {
                StringSource source(dump);
                restorePath(dstPath, source);
            } else
                writeFile(dstPath, dump);

            canonicalisePathMetaData(dstPath, -1);

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

            optimisePath(dstPath); // FIXME: combine with hashPath()

            ValidPathInfo info;
            info.path = dstPath;
            info.narHash = hash.first;
            info.narSize = hash.second;
            registerValidPath(info);
        }

        outputLock.setDeletion(true);
    }

    return dstPath;
}


Path LocalStore::addToStore(const string & name, const Path & _srcPath,
    bool recursive, HashType hashAlgo, PathFilter & filter, bool repair)
{
    Path srcPath(absPath(_srcPath));
    debug(format("adding ‘%1%’ to the store") % srcPath);

    /* Read the whole path into memory. This is not a very scalable
       method for very large paths, but `copyPath' is mainly used for
       small files. */
    StringSink sink;
    if (recursive)
        dumpPath(srcPath, sink, filter);
    else
        sink.s = make_ref<std::string>(readFile(srcPath));

    return addToStoreFromDump(*sink.s, name, recursive, hashAlgo, repair);
}


Path LocalStore::addTextToStore(const string & name, const string & s,
    const PathSet & references, bool repair)
{
    Path dstPath = computeStorePathForText(name, s, references);

    addTempRoot(dstPath);

    if (repair || !isValidPath(dstPath)) {

        PathLocks outputLock(singleton<PathSet, Path>(dstPath));

        if (repair || !isValidPath(dstPath)) {

            deletePath(dstPath);

            writeFile(dstPath, s);

            canonicalisePathMetaData(dstPath, -1);

            StringSink sink;
            dumpString(s, sink);
            auto hash = hashString(htSHA256, *sink.s);

            optimisePath(dstPath);

            ValidPathInfo info;
            info.path = dstPath;
            info.narHash = hash;
            info.narSize = sink.s->size();
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
    virtual void operator () (const unsigned char * data, size_t len)
    {
        writeSink(data, len);
        hashSink(data, len);
    }
    Hash currentHash()
    {
        return hashSink.currentHash().first;
    }
};


static void checkSecrecy(const Path & path)
{
    struct stat st;
    if (stat(path.c_str(), &st))
        throw SysError(format("getting status of ‘%1%’") % path);
    if ((st.st_mode & (S_IRWXG | S_IRWXO)) != 0)
        throw Error(format("file ‘%1%’ should be secret (inaccessible to everybody else)!") % path);
}


void LocalStore::exportPath(const Path & path, bool sign,
    Sink & sink)
{
    assertStorePath(path);

    printMsg(lvlTalkative, format("exporting path ‘%1%’") % path);

    if (!isValidPath(path))
        throw Error(format("path ‘%1%’ is not valid") % path);

    HashAndWriteSink hashAndWriteSink(sink);

    dumpPath(path, hashAndWriteSink);

    /* Refuse to export paths that have changed.  This prevents
       filesystem corruption from spreading to other machines.
       Don't complain if the stored hash is zero (unknown). */
    Hash hash = hashAndWriteSink.currentHash();
    Hash storedHash = queryPathHash(path);
    if (hash != storedHash && storedHash != Hash(storedHash.type))
        throw Error(format("hash of path ‘%1%’ has changed from ‘%2%’ to ‘%3%’!") % path
            % printHash(storedHash) % printHash(hash));

    PathSet references;
    queryReferences(path, references);

    hashAndWriteSink << exportMagic << path << references << queryDeriver(path);

    if (sign) {
        Hash hash = hashAndWriteSink.currentHash();

        Path tmpDir = createTempDir();
        AutoDelete delTmp(tmpDir);
        Path hashFile = tmpDir + "/hash";
        writeFile(hashFile, printHash(hash));

        Path secretKey = settings.nixConfDir + "/signing-key.sec";
        checkSecrecy(secretKey);

        Strings args;
        args.push_back("rsautl");
        args.push_back("-sign");
        args.push_back("-inkey");
        args.push_back(secretKey);
        args.push_back("-in");
        args.push_back(hashFile);
        string signature = runProgram(OPENSSL_PATH, true, args);

        hashAndWriteSink << 1 << signature;

    } else
        hashAndWriteSink << 0;
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
    size_t read(unsigned char * data, size_t len)
    {
        size_t n = readSource.read(data, len);
        if (hashing) hashSink(data, n);
        return n;
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
        tmpDir = createTempDir(settings.nixStore);
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

    uint32_t magic = readInt(hashAndReadSource);
    if (magic != exportMagic)
        throw Error("Nix archive cannot be imported; wrong format");

    Path dstPath = readStorePath(hashAndReadSource);

    printMsg(lvlTalkative, format("importing path ‘%1%’") % dstPath);

    PathSet references = readStorePaths<PathSet>(hashAndReadSource);

    Path deriver = readString(hashAndReadSource);
    if (deriver != "") assertStorePath(deriver);

    Hash hash = hashAndReadSource.hashSink.finish().first;
    hashAndReadSource.hashing = false;

    bool haveSignature = readInt(hashAndReadSource) == 1;

    if (requireSignature && !haveSignature)
        throw Error(format("imported archive of ‘%1%’ lacks a signature") % dstPath);

    if (haveSignature) {
        string signature = readString(hashAndReadSource);

        if (requireSignature) {
            Path sigFile = tmpDir + "/sig";
            writeFile(sigFile, signature);

            Strings args;
            args.push_back("rsautl");
            args.push_back("-verify");
            args.push_back("-inkey");
            args.push_back(settings.nixConfDir + "/signing-key.pub");
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
        Strings locksHeld = tokenizeString<Strings>(getEnv("NIX_HELD_LOCKS"));
        if (find(locksHeld.begin(), locksHeld.end(), dstPath) == locksHeld.end())
            outputLock.lockPaths(singleton<PathSet, Path>(dstPath));

        if (!isValidPath(dstPath)) {

            deletePath(dstPath);

            if (rename(unpacked.c_str(), dstPath.c_str()) == -1)
                throw SysError(format("cannot move ‘%1%’ to ‘%2%’")
                    % unpacked % dstPath);

            canonicalisePathMetaData(dstPath, -1);

            /* !!! if we were clever, we could prevent the hashPath()
               here. */
            HashResult hash = hashPath(htSHA256, dstPath);

            optimisePath(dstPath); // FIXME: combine with hashPath()

            ValidPathInfo info;
            info.path = dstPath;
            info.narHash = hash.first;
            info.narSize = hash.second;
            info.references = references;
            info.deriver = deriver != "" && isValidPath(deriver) ? deriver : "";
            registerValidPath(info);
        }

        outputLock.setDeletion(true);
    }

    return dstPath;
}


Paths LocalStore::importPaths(bool requireSignature, Source & source,
    std::shared_ptr<FSAccessor> accessor)
{
    Paths res;
    while (true) {
        unsigned long long n = readLongLong(source);
        if (n == 0) break;
        if (n != 1) throw Error("input doesn't look like something created by ‘nix-store --export’");
        res.push_back(importPath(requireSignature, source));
    }
    return res;
}


void LocalStore::invalidatePathChecked(const Path & path)
{
    assertStorePath(path);

    retrySQLite<void>([&]() {
        SQLiteTxn txn(db);

        if (isValidPath_(path)) {
            PathSet referrers; queryReferrers_(path, referrers);
            referrers.erase(path); /* ignore self-references */
            if (!referrers.empty())
                throw PathInUse(format("cannot delete path ‘%1%’ because it is in use by %2%")
                    % path % showPaths(referrers));
            invalidatePath(path);
        }

        txn.commit();
    });
}


bool LocalStore::verifyStore(bool checkContents, bool repair)
{
    printMsg(lvlError, format("reading the Nix store..."));

    bool errors = false;

    /* Acquire the global GC lock to prevent a garbage collection. */
    AutoCloseFD fdGCLock = openGCLock(ltWrite);

    PathSet store;
    for (auto & i : readDirectory(settings.nixStore)) store.insert(i.name);

    /* Check whether all valid paths actually exist. */
    printMsg(lvlInfo, "checking path existence...");

    PathSet validPaths2 = queryAllValidPaths(), validPaths, done;

    for (auto & i : validPaths2)
        verifyPath(i, store, done, validPaths, repair, errors);

    /* Release the GC lock so that checking content hashes (which can
       take ages) doesn't block the GC or builds. */
    fdGCLock.close();

    /* Optionally, check the content hashes (slow). */
    if (checkContents) {
        printMsg(lvlInfo, "checking hashes...");

        Hash nullHash(htSHA256);

        for (auto & i : validPaths) {
            try {
                ValidPathInfo info = queryPathInfo(i);

                /* Check the content hash (optionally - slow). */
                printMsg(lvlTalkative, format("checking contents of ‘%1%’") % i);
                HashResult current = hashPath(info.narHash.type, i);

                if (info.narHash != nullHash && info.narHash != current.first) {
                    printMsg(lvlError, format("path ‘%1%’ was modified! "
                            "expected hash ‘%2%’, got ‘%3%’")
                        % i % printHash(info.narHash) % printHash(current.first));
                    if (repair) repairPath(i); else errors = true;
                } else {

                    bool update = false;

                    /* Fill in missing hashes. */
                    if (info.narHash == nullHash) {
                        printMsg(lvlError, format("fixing missing hash on ‘%1%’") % i);
                        info.narHash = current.first;
                        update = true;
                    }

                    /* Fill in missing narSize fields (from old stores). */
                    if (info.narSize == 0) {
                        printMsg(lvlError, format("updating size field on ‘%1%’ to %2%") % i % current.second);
                        info.narSize = current.second;
                        update = true;
                    }

                    if (update) updatePathInfo(info);

                }

            } catch (Error & e) {
                /* It's possible that the path got GC'ed, so ignore
                   errors on invalid paths. */
                if (isValidPath(i))
                    printMsg(lvlError, format("error: %1%") % e.msg());
                else
                    printMsg(lvlError, format("warning: %1%") % e.msg());
                errors = true;
            }
        }
    }

    return errors;
}


void LocalStore::verifyPath(const Path & path, const PathSet & store,
    PathSet & done, PathSet & validPaths, bool repair, bool & errors)
{
    checkInterrupt();

    if (done.find(path) != done.end()) return;
    done.insert(path);

    if (!isStorePath(path)) {
        printMsg(lvlError, format("path ‘%1%’ is not in the Nix store") % path);
        invalidatePath(path);
        return;
    }

    if (store.find(baseNameOf(path)) == store.end()) {
        /* Check any referrers first.  If we can invalidate them
           first, then we can invalidate this path as well. */
        bool canInvalidate = true;
        PathSet referrers; queryReferrers(path, referrers);
        for (auto & i : referrers)
            if (i != path) {
                verifyPath(i, store, done, validPaths, repair, errors);
                if (validPaths.find(i) != validPaths.end())
                    canInvalidate = false;
            }

        if (canInvalidate) {
            printMsg(lvlError, format("path ‘%1%’ disappeared, removing from database...") % path);
            invalidatePath(path);
        } else {
            printMsg(lvlError, format("path ‘%1%’ disappeared, but it still has valid referrers!") % path);
            if (repair)
                try {
                    repairPath(path);
                } catch (Error & e) {
                    printMsg(lvlError, format("warning: %1%") % e.msg());
                    errors = true;
                }
            else errors = true;
        }

        return;
    }

    validPaths.insert(path);
}


bool LocalStore::pathContentsGood(const Path & path)
{
    std::map<Path, bool>::iterator i = pathContentsGoodCache.find(path);
    if (i != pathContentsGoodCache.end()) return i->second;
    printMsg(lvlInfo, format("checking path ‘%1%’...") % path);
    ValidPathInfo info = queryPathInfo(path);
    bool res;
    if (!pathExists(path))
        res = false;
    else {
        HashResult current = hashPath(info.narHash.type, path);
        Hash nullHash(htSHA256);
        res = info.narHash == nullHash || info.narHash == current.first;
    }
    pathContentsGoodCache[path] = res;
    if (!res) printMsg(lvlError, format("path ‘%1%’ is corrupted or missing!") % path);
    return res;
}


void LocalStore::markContentsGood(const Path & path)
{
    pathContentsGoodCache[path] = true;
}


#if defined(FS_IOC_SETFLAGS) && defined(FS_IOC_GETFLAGS) && defined(FS_IMMUTABLE_FL)

static void makeMutable(const Path & path)
{
    checkInterrupt();

    struct stat st = lstat(path);

    if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) return;

    if (S_ISDIR(st.st_mode)) {
        for (auto & i : readDirectory(path))
            makeMutable(path + "/" + i.name);
    }

    /* The O_NOFOLLOW is important to prevent us from changing the
       mutable bit on the target of a symlink (which would be a
       security hole). */
    AutoCloseFD fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW);
    if (fd == -1) {
        if (errno == ELOOP) return; // it's a symlink
        throw SysError(format("opening file ‘%1%’") % path);
    }

    unsigned int flags = 0, old;

    /* Silently ignore errors getting/setting the immutable flag so
       that we work correctly on filesystems that don't support it. */
    if (ioctl(fd, FS_IOC_GETFLAGS, &flags)) return;
    old = flags;
    flags &= ~FS_IMMUTABLE_FL;
    if (old == flags) return;
    if (ioctl(fd, FS_IOC_SETFLAGS, &flags)) return;
}

/* Upgrade from schema 6 (Nix 0.15) to schema 7 (Nix >= 1.3). */
void LocalStore::upgradeStore7()
{
    if (getuid() != 0) return;
    printMsg(lvlError, "removing immutable bits from the Nix store (this may take a while)...");
    makeMutable(settings.nixStore);
}

#else

void LocalStore::upgradeStore7()
{
}

#endif


void LocalStore::vacuumDB()
{
    if (sqlite3_exec(db, "vacuum;", 0, 0, 0) != SQLITE_OK)
        throwSQLiteError(db, "vacuuming SQLite database");
}


}
