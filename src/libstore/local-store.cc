#include "local-store.hh"
#include "globals.hh"
#include "archive.hh"
#include "pathlocks.hh"
#include "worker-protocol.hh"
#include "derivations.hh"
#include "nar-info.hh"
#include "references.hh"

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
#include <sys/xattr.h>
#endif

#ifdef __CYGWIN__
#include <windows.h>
#endif

#include <sqlite3.h>


namespace nix {


LocalStore::LocalStore(const Params & params)
    : Store(params)
    , LocalFSStore(params)
    , realStoreDir_{this, false, rootDir != "" ? rootDir + "/nix/store" : storeDir, "real",
        "physical path to the Nix store"}
    , realStoreDir(realStoreDir_)
    , dbDir(stateDir + "/db")
    , linksDir(realStoreDir + "/.links")
    , reservedPath(dbDir + "/reserved")
    , schemaPath(dbDir + "/schema")
    , trashDir(realStoreDir + "/trash")
    , tempRootsDir(stateDir + "/temproots")
    , fnTempRoots(fmt("%s/%d", tempRootsDir, getpid()))
    , locksHeld(tokenizeString<PathSet>(getEnv("NIX_HELD_LOCKS").value_or("")))
{
    auto state(_state.lock());

    /* Create missing state directories if they don't already exist. */
    createDirs(realStoreDir);
    makeStoreWritable();
    createDirs(linksDir);
    Path profilesDir = stateDir + "/profiles";
    createDirs(profilesDir);
    createDirs(tempRootsDir);
    createDirs(dbDir);
    Path gcRootsDir = stateDir + "/gcroots";
    if (!pathExists(gcRootsDir)) {
        createDirs(gcRootsDir);
        createSymlink(profilesDir, gcRootsDir + "/profiles");
    }

    for (auto & perUserDir : {profilesDir + "/per-user", gcRootsDir + "/per-user"}) {
        createDirs(perUserDir);
        if (chmod(perUserDir.c_str(), 0755) == -1)
            throw SysError("could not set permissions on '%s' to 755", perUserDir);
    }

    createUser(getUserName(), getuid());

    /* Optionally, create directories and set permissions for a
       multi-user install. */
    if (getuid() == 0 && settings.buildUsersGroup != "") {
        mode_t perm = 01775;

        struct group * gr = getgrnam(settings.buildUsersGroup.get().c_str());
        if (!gr)
            printError(format("warning: the group '%1%' specified in 'build-users-group' does not exist")
                % settings.buildUsersGroup);
        else {
            struct stat st;
            if (stat(realStoreDir.c_str(), &st))
                throw SysError(format("getting attributes of path '%1%'") % realStoreDir);

            if (st.st_uid != 0 || st.st_gid != gr->gr_gid || (st.st_mode & ~S_IFMT) != perm) {
                if (chown(realStoreDir.c_str(), 0, gr->gr_gid) == -1)
                    throw SysError(format("changing ownership of path '%1%'") % realStoreDir);
                if (chmod(realStoreDir.c_str(), perm) == -1)
                    throw SysError(format("changing permissions on path '%1%'") % realStoreDir);
            }
        }
    }

    /* Ensure that the store and its parents are not symlinks. */
    if (getEnv("NIX_IGNORE_SYMLINK_STORE") != "1") {
        Path path = realStoreDir;
        struct stat st;
        while (path != "/") {
            if (lstat(path.c_str(), &st))
                throw SysError(format("getting status of '%1%'") % path);
            if (S_ISLNK(st.st_mode))
                throw Error(format(
                        "the path '%1%' is a symlink; "
                        "this is not allowed for the Nix store and its parent directories")
                    % path);
            path = dirOf(path);
        }
    }

    /* We can't open a SQLite database if the disk is full.  Since
       this prevents the garbage collector from running when it's most
       needed, we reserve some dummy space that we can free just
       before doing a garbage collection. */
    try {
        struct stat st;
        if (stat(reservedPath.c_str(), &st) == -1 ||
            st.st_size != settings.reservedSize)
        {
            AutoCloseFD fd = open(reservedPath.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
            int res = -1;
#if HAVE_POSIX_FALLOCATE
            res = posix_fallocate(fd.get(), 0, settings.reservedSize);
#endif
            if (res == -1) {
                writeFull(fd.get(), string(settings.reservedSize, 'X'));
                [[gnu::unused]] auto res2 = ftruncate(fd.get(), settings.reservedSize);
            }
        }
    } catch (SysError & e) { /* don't care about errors */
    }

    /* Acquire the big fat lock in shared mode to make sure that no
       schema upgrade is in progress. */
    Path globalLockPath = dbDir + "/big-lock";
    globalLock = openLockFile(globalLockPath.c_str(), true);

    if (!lockFile(globalLock.get(), ltRead, false)) {
        printError("waiting for the big Nix store lock...");
        lockFile(globalLock.get(), ltRead, true);
    }

    /* Check the current database schema and if necessary do an
       upgrade.  */
    int curSchema = getSchema();
    if (curSchema > nixSchemaVersion)
        throw Error(format("current Nix store schema is version %1%, but I only support %2%")
            % curSchema % nixSchemaVersion);

    else if (curSchema == 0) { /* new store */
        curSchema = nixSchemaVersion;
        openDB(*state, true);
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

        if (!lockFile(globalLock.get(), ltWrite, false)) {
            printError("waiting for exclusive access to the Nix store...");
            lockFile(globalLock.get(), ltWrite, true);
        }

        /* Get the schema version again, because another process may
           have performed the upgrade already. */
        curSchema = getSchema();

        if (curSchema < 7) { upgradeStore7(); }

        openDB(*state, false);

        if (curSchema < 8) {
            SQLiteTxn txn(state->db);
            state->db.exec("alter table ValidPaths add column ultimate integer");
            state->db.exec("alter table ValidPaths add column sigs text");
            txn.commit();
        }

        if (curSchema < 9) {
            SQLiteTxn txn(state->db);
            state->db.exec("drop table FailedPaths");
            txn.commit();
        }

        if (curSchema < 10) {
            SQLiteTxn txn(state->db);
            state->db.exec("alter table ValidPaths add column ca text");
            txn.commit();
        }

        writeFile(schemaPath, (format("%1%") % nixSchemaVersion).str());

        lockFile(globalLock.get(), ltRead, true);
    }

    else openDB(*state, false);

    /* Prepare SQL statements. */
    state->stmtRegisterValidPath.create(state->db,
        "insert into ValidPaths (path, hash, registrationTime, deriver, narSize, ultimate, sigs, ca) values (?, ?, ?, ?, ?, ?, ?, ?);");
    state->stmtUpdatePathInfo.create(state->db,
        "update ValidPaths set narSize = ?, hash = ?, ultimate = ?, sigs = ?, ca = ? where path = ?;");
    state->stmtAddReference.create(state->db,
        "insert or replace into Refs (referrer, reference) values (?, ?);");
    state->stmtQueryPathInfo.create(state->db,
        "select id, hash, registrationTime, deriver, narSize, ultimate, sigs, ca from ValidPaths where path = ?;");
    state->stmtQueryReferences.create(state->db,
        "select path from Refs join ValidPaths on reference = id where referrer = ?;");
    state->stmtQueryReferrers.create(state->db,
        "select path from Refs join ValidPaths on referrer = id where reference = (select id from ValidPaths where path = ?);");
    state->stmtInvalidatePath.create(state->db,
        "delete from ValidPaths where path = ?;");
    state->stmtAddDerivationOutput.create(state->db,
        "insert or replace into DerivationOutputs (drv, id, path) values (?, ?, ?);");
    state->stmtQueryValidDerivers.create(state->db,
        "select v.id, v.path from DerivationOutputs d join ValidPaths v on d.drv = v.id where d.path = ?;");
    state->stmtQueryDerivationOutputs.create(state->db,
        "select id, path from DerivationOutputs where drv = ?;");
    // Use "path >= ?" with limit 1 rather than "path like '?%'" to
    // ensure efficient lookup.
    state->stmtQueryPathFromHashPart.create(state->db,
        "select path from ValidPaths where path >= ? limit 1;");
    state->stmtQueryValidPaths.create(state->db, "select path from ValidPaths");
}


LocalStore::~LocalStore()
{
    std::shared_future<void> future;

    {
        auto state(_state.lock());
        if (state->gcRunning)
            future = state->gcFuture;
    }

    if (future.valid()) {
        printError("waiting for auto-GC to finish on exit...");
        future.get();
    }

    try {
        auto state(_state.lock());
        if (state->fdTempRoots) {
            state->fdTempRoots = -1;
            unlink(fnTempRoots.c_str());
        }
    } catch (...) {
        ignoreException();
    }
}


std::string LocalStore::getUri()
{
    return "local";
}


int LocalStore::getSchema()
{
    int curSchema = 0;
    if (pathExists(schemaPath)) {
        string s = readFile(schemaPath);
        if (!string2Int(s, curSchema))
            throw Error(format("'%1%' is corrupt") % schemaPath);
    }
    return curSchema;
}


void LocalStore::openDB(State & state, bool create)
{
    if (access(dbDir.c_str(), R_OK | W_OK))
        throw SysError(format("Nix database directory '%1%' is not writable") % dbDir);

    /* Open the Nix database. */
    string dbPath = dbDir + "/db.sqlite";
    auto & db(state.db);
    state.db = SQLite(dbPath, create);

#ifdef __CYGWIN__
    /* The cygwin version of sqlite3 has a patch which calls
       SetDllDirectory("/usr/bin") on init. It was intended to fix extension
       loading, which we don't use, and the effect of SetDllDirectory is
       inherited by child processes, and causes libraries to be loaded from
       /usr/bin instead of $PATH. This breaks quite a few things (e.g.
       checkPhase on openssh), so we set it back to default behaviour. */
    SetDllDirectoryW(L"");
#endif

    /* !!! check whether sqlite has been built with foreign key
       support */

    /* Whether SQLite should fsync().  "Normal" synchronous mode
       should be safe enough.  If the user asks for it, don't sync at
       all.  This can cause database corruption if the system
       crashes. */
    string syncMode = settings.fsyncMetadata ? "normal" : "off";
    db.exec("pragma synchronous = " + syncMode);

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
        static const char schema[] =
#include "schema.sql.gen.hh"
            ;
        db.exec(schema);
    }
}


/* To improve purity, users may want to make the Nix store a read-only
   bind mount.  So make the Nix store writable for this process. */
void LocalStore::makeStoreWritable()
{
#if __linux__
    if (getuid() != 0) return;
    /* Check if /nix/store is on a read-only mount. */
    struct statvfs stat;
    if (statvfs(realStoreDir.c_str(), &stat) != 0)
        throw SysError("getting info about the Nix store mount point");

    if (stat.f_flag & ST_RDONLY) {
        if (unshare(CLONE_NEWNS) == -1)
            throw SysError("setting up a private mount namespace");

        if (mount(0, realStoreDir.c_str(), "none", MS_REMOUNT | MS_BIND, 0) == -1)
            throw SysError(format("remounting %1% writable") % realStoreDir);
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
                throw SysError(format("changing mode of '%1%' to %2$o") % path % mode);
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
            throw SysError(format("changing modification time of '%1%'") % path);
    }
}


void canonicaliseTimestampAndPermissions(const Path & path)
{
    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError(format("getting attributes of path '%1%'") % path);
    canonicaliseTimestampAndPermissions(path, st);
}


static void canonicalisePathMetaData_(const Path & path, uid_t fromUid, InodesSeen & inodesSeen)
{
    checkInterrupt();

#if __APPLE__
    /* Remove flags, in particular UF_IMMUTABLE which would prevent
       the file from being garbage-collected. FIXME: Use
       setattrlist() to remove other attributes as well. */
    if (lchflags(path.c_str(), 0)) {
        if (errno != ENOTSUP)
            throw SysError(format("clearing flags of path '%1%'") % path);
    }
#endif

    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError(format("getting attributes of path '%1%'") % path);

    /* Really make sure that the path is of a supported type. */
    if (!(S_ISREG(st.st_mode) || S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)))
        throw Error(format("file '%1%' has an unsupported type") % path);

#if __linux__
    /* Remove extended attributes / ACLs. */
    ssize_t eaSize = llistxattr(path.c_str(), nullptr, 0);

    if (eaSize < 0) {
        if (errno != ENOTSUP && errno != ENODATA)
            throw SysError("querying extended attributes of '%s'", path);
    } else if (eaSize > 0) {
        std::vector<char> eaBuf(eaSize);

        if ((eaSize = llistxattr(path.c_str(), eaBuf.data(), eaBuf.size())) < 0)
            throw SysError("querying extended attributes of '%s'", path);

        for (auto & eaName: tokenizeString<Strings>(std::string(eaBuf.data(), eaSize), std::string("\000", 1))) {
            /* Ignore SELinux security labels since these cannot be
               removed even by root. */
            if (eaName == "security.selinux") continue;
            if (lremovexattr(path.c_str(), eaName.c_str()) == -1)
                throw SysError("removing extended attribute '%s' from '%s'", eaName, path);
        }
     }
#endif

    /* Fail if the file is not owned by the build user.  This prevents
       us from messing up the ownership/permissions of files
       hard-linked into the output (e.g. "ln /etc/shadow $out/foo").
       However, ignore files that we chown'ed ourselves previously to
       ensure that we don't fail on hard links within the same build
       (i.e. "touch $out/foo; ln $out/foo $out/bar"). */
    if (fromUid != (uid_t) -1 && st.st_uid != fromUid) {
        assert(!S_ISDIR(st.st_mode));
        if (inodesSeen.find(Inode(st.st_dev, st.st_ino)) == inodesSeen.end())
            throw BuildError(format("invalid ownership on file '%1%'") % path);
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
            throw SysError(format("changing owner of '%1%' to %2%")
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
        throw SysError(format("getting attributes of path '%1%'") % path);

    if (st.st_uid != geteuid()) {
        assert(S_ISLNK(st.st_mode));
        throw Error(format("wrong ownership of top-level store path '%1%'") % path);
    }
}


void canonicalisePathMetaData(const Path & path, uid_t fromUid)
{
    InodesSeen inodesSeen;
    canonicalisePathMetaData(path, fromUid, inodesSeen);
}


void LocalStore::checkDerivationOutputs(const StorePath & drvPath, const Derivation & drv)
{
    assert(drvPath.isDerivation());
    std::string drvName(drvPath.name());
    drvName = string(drvName, 0, drvName.size() - drvExtension.size());

    auto check = [&](const StorePath & expected, const StorePath & actual, const std::string & varName)
    {
        if (actual != expected)
            throw Error("derivation '%s' has incorrect output '%s', should be '%s'",
                printStorePath(drvPath), printStorePath(actual), printStorePath(expected));
        auto j = drv.env.find(varName);
        if (j == drv.env.end() || parseStorePath(j->second) != actual)
            throw Error("derivation '%s' has incorrect environment variable '%s', should be '%s'",
                printStorePath(drvPath), varName, printStorePath(actual));
    };


    if (drv.isFixedOutput()) {
        DerivationOutputs::const_iterator out = drv.outputs.find("out");
        if (out == drv.outputs.end())
            throw Error("derivation '%s' does not have an output named 'out'", printStorePath(drvPath));

        bool recursive; Hash h;
        out->second.parseHashInfo(recursive, h);

        check(makeFixedOutputPath(recursive, h, drvName), out->second.path, "out");
    }

    else {
        Hash h = hashDerivationModulo(*this, drv, true);
        for (auto & i : drv.outputs)
            check(makeOutputPath(i.first, h, drvName), i.second.path, i.first);
    }
}


uint64_t LocalStore::addValidPath(State & state,
    const ValidPathInfo & info, bool checkOutputs)
{
    if (info.ca != "" && !info.isContentAddressed(*this))
        throw Error("cannot add path '%s' to the Nix store because it claims to be content-addressed but isn't",
            printStorePath(info.path));

    state.stmtRegisterValidPath.use()
        (printStorePath(info.path))
        (info.narHash.to_string(Base16))
        (info.registrationTime == 0 ? time(0) : info.registrationTime)
        (info.deriver ? printStorePath(*info.deriver) : "", (bool) info.deriver)
        (info.narSize, info.narSize != 0)
        (info.ultimate ? 1 : 0, info.ultimate)
        (concatStringsSep(" ", info.sigs), !info.sigs.empty())
        (info.ca, !info.ca.empty())
        .exec();
    uint64_t id = sqlite3_last_insert_rowid(state.db);

    /* If this is a derivation, then store the derivation outputs in
       the database.  This is useful for the garbage collector: it can
       efficiently query whether a path is an output of some
       derivation. */
    if (info.path.isDerivation()) {
        auto drv = readDerivation(*this, realStoreDir + "/" + std::string(info.path.to_string()));

        /* Verify that the output paths in the derivation are correct
           (i.e., follow the scheme for computing output paths from
           derivations).  Note that if this throws an error, then the
           DB transaction is rolled back, so the path validity
           registration above is undone. */
        if (checkOutputs) checkDerivationOutputs(info.path, drv);

        for (auto & i : drv.outputs) {
            state.stmtAddDerivationOutput.use()
                (id)
                (i.first)
                (printStorePath(i.second.path))
                .exec();
        }
    }

    {
        auto state_(Store::state.lock());
        state_->pathInfoCache.upsert(storePathToHash(printStorePath(info.path)),
            PathInfoCacheValue{ .value = std::make_shared<const ValidPathInfo>(info) });
    }

    return id;
}


void LocalStore::queryPathInfoUncached(const StorePath & path,
    Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept
{
    try {
        auto info = std::make_shared<ValidPathInfo>(path.clone());

        callback(retrySQLite<std::shared_ptr<ValidPathInfo>>([&]() {
            auto state(_state.lock());

            /* Get the path info. */
            auto useQueryPathInfo(state->stmtQueryPathInfo.use()(printStorePath(info->path)));

            if (!useQueryPathInfo.next())
                return std::shared_ptr<ValidPathInfo>();

            info->id = useQueryPathInfo.getInt(0);

            try {
                info->narHash = Hash(useQueryPathInfo.getStr(1));
            } catch (BadHash & e) {
                throw Error("in valid-path entry for '%s': %s", printStorePath(path), e.what());
            }

            info->registrationTime = useQueryPathInfo.getInt(2);

            auto s = (const char *) sqlite3_column_text(state->stmtQueryPathInfo, 3);
            if (s) info->deriver = parseStorePath(s);

            /* Note that narSize = NULL yields 0. */
            info->narSize = useQueryPathInfo.getInt(4);

            info->ultimate = useQueryPathInfo.getInt(5) == 1;

            s = (const char *) sqlite3_column_text(state->stmtQueryPathInfo, 6);
            if (s) info->sigs = tokenizeString<StringSet>(s, " ");

            s = (const char *) sqlite3_column_text(state->stmtQueryPathInfo, 7);
            if (s) info->ca = s;

            /* Get the references. */
            auto useQueryReferences(state->stmtQueryReferences.use()(info->id));

            while (useQueryReferences.next())
                info->references.insert(parseStorePath(useQueryReferences.getStr(0)));

            return info;
        }));

    } catch (...) { callback.rethrow(); }
}


/* Update path info in the database. */
void LocalStore::updatePathInfo(State & state, const ValidPathInfo & info)
{
    state.stmtUpdatePathInfo.use()
        (info.narSize, info.narSize != 0)
        (info.narHash.to_string(Base16))
        (info.ultimate ? 1 : 0, info.ultimate)
        (concatStringsSep(" ", info.sigs), !info.sigs.empty())
        (info.ca, !info.ca.empty())
        (printStorePath(info.path))
        .exec();
}


uint64_t LocalStore::queryValidPathId(State & state, const StorePath & path)
{
    auto use(state.stmtQueryPathInfo.use()(printStorePath(path)));
    if (!use.next())
        throw Error("path '%s' is not valid", printStorePath(path));
    return use.getInt(0);
}


bool LocalStore::isValidPath_(State & state, const StorePath & path)
{
    return state.stmtQueryPathInfo.use()(printStorePath(path)).next();
}


bool LocalStore::isValidPathUncached(const StorePath & path)
{
    return retrySQLite<bool>([&]() {
        auto state(_state.lock());
        return isValidPath_(*state, path);
    });
}


StorePathSet LocalStore::queryValidPaths(const StorePathSet & paths, SubstituteFlag maybeSubstitute)
{
    StorePathSet res;
    for (auto & i : paths)
        if (isValidPath(i)) res.insert(i.clone());
    return res;
}


StorePathSet LocalStore::queryAllValidPaths()
{
    return retrySQLite<StorePathSet>([&]() {
        auto state(_state.lock());
        auto use(state->stmtQueryValidPaths.use());
        StorePathSet res;
        while (use.next()) res.insert(parseStorePath(use.getStr(0)));
        return res;
    });
}


void LocalStore::queryReferrers(State & state, const StorePath & path, StorePathSet & referrers)
{
    auto useQueryReferrers(state.stmtQueryReferrers.use()(printStorePath(path)));

    while (useQueryReferrers.next())
        referrers.insert(parseStorePath(useQueryReferrers.getStr(0)));
}


void LocalStore::queryReferrers(const StorePath & path, StorePathSet & referrers)
{
    return retrySQLite<void>([&]() {
        auto state(_state.lock());
        queryReferrers(*state, path, referrers);
    });
}


StorePathSet LocalStore::queryValidDerivers(const StorePath & path)
{
    return retrySQLite<StorePathSet>([&]() {
        auto state(_state.lock());

        auto useQueryValidDerivers(state->stmtQueryValidDerivers.use()(printStorePath(path)));

        StorePathSet derivers;
        while (useQueryValidDerivers.next())
            derivers.insert(parseStorePath(useQueryValidDerivers.getStr(1)));

        return derivers;
    });
}


StorePathSet LocalStore::queryDerivationOutputs(const StorePath & path)
{
    return retrySQLite<StorePathSet>([&]() {
        auto state(_state.lock());

        auto useQueryDerivationOutputs(state->stmtQueryDerivationOutputs.use()
            (queryValidPathId(*state, path)));

        StorePathSet outputs;
        while (useQueryDerivationOutputs.next())
            outputs.insert(parseStorePath(useQueryDerivationOutputs.getStr(1)));

        return outputs;
    });
}


StringSet LocalStore::queryDerivationOutputNames(const StorePath & path)
{
    return retrySQLite<StringSet>([&]() {
        auto state(_state.lock());

        auto useQueryDerivationOutputs(state->stmtQueryDerivationOutputs.use()
            (queryValidPathId(*state, path)));

        StringSet outputNames;
        while (useQueryDerivationOutputs.next())
            outputNames.insert(useQueryDerivationOutputs.getStr(0));

        return outputNames;
    });
}


std::optional<StorePath> LocalStore::queryPathFromHashPart(const std::string & hashPart)
{
    if (hashPart.size() != storePathHashLen) throw Error("invalid hash part");

    Path prefix = storeDir + "/" + hashPart;

    return retrySQLite<std::optional<StorePath>>([&]() -> std::optional<StorePath> {
        auto state(_state.lock());

        auto useQueryPathFromHashPart(state->stmtQueryPathFromHashPart.use()(prefix));

        if (!useQueryPathFromHashPart.next()) return {};

        const char * s = (const char *) sqlite3_column_text(state->stmtQueryPathFromHashPart, 0);
        if (s && prefix.compare(0, prefix.size(), s, prefix.size()) == 0)
            return parseStorePath(s);
        return {};
    });
}


StorePathSet LocalStore::querySubstitutablePaths(const StorePathSet & paths)
{
    if (!settings.useSubstitutes) return StorePathSet();

    StorePathSet remaining;
    for (auto & i : paths)
        remaining.insert(i.clone());

    StorePathSet res;

    for (auto & sub : getDefaultSubstituters()) {
        if (remaining.empty()) break;
        if (sub->storeDir != storeDir) continue;
        if (!sub->wantMassQuery) continue;

        auto valid = sub->queryValidPaths(remaining);

        StorePathSet remaining2;
        for (auto & path : remaining)
            if (valid.count(path))
                res.insert(path.clone());
            else
                remaining2.insert(path.clone());

        std::swap(remaining, remaining2);
    }

    return res;
}


void LocalStore::querySubstitutablePathInfos(const StorePathSet & paths,
    SubstitutablePathInfos & infos)
{
    if (!settings.useSubstitutes) return;
    for (auto & sub : getDefaultSubstituters()) {
        if (sub->storeDir != storeDir) continue;
        for (auto & path : paths) {
            if (infos.count(path)) continue;
            debug("checking substituter '%s' for path '%s'", sub->getUri(), printStorePath(path));
            try {
                auto info = sub->queryPathInfo(path);
                auto narInfo = std::dynamic_pointer_cast<const NarInfo>(
                    std::shared_ptr<const ValidPathInfo>(info));
                infos.insert_or_assign(path.clone(), SubstitutablePathInfo{
                    info->deriver ? info->deriver->clone() : std::optional<StorePath>(),
                    cloneStorePathSet(info->references),
                    narInfo ? narInfo->fileSize : 0,
                    info->narSize});
            } catch (InvalidPath &) {
            } catch (SubstituterDisabled &) {
            } catch (Error & e) {
                if (settings.tryFallback)
                    printError(e.what());
                else
                    throw;
            }
        }
    }
}


void LocalStore::registerValidPath(const ValidPathInfo & info)
{
    ValidPathInfos infos;
    infos.push_back(info);
    registerValidPaths(infos);
}


void LocalStore::registerValidPaths(const ValidPathInfos & infos)
{
    /* SQLite will fsync by default, but the new valid paths may not
       be fsync-ed.  So some may want to fsync them before registering
       the validity, at the expense of some speed of the path
       registering operation. */
    if (settings.syncBeforeRegistering) sync();

    return retrySQLite<void>([&]() {
        auto state(_state.lock());

        SQLiteTxn txn(state->db);
        StorePathSet paths;

        for (auto & i : infos) {
            assert(i.narHash.type == htSHA256);
            if (isValidPath_(*state, i.path))
                updatePathInfo(*state, i);
            else
                addValidPath(*state, i, false);
            paths.insert(i.path.clone());
        }

        for (auto & i : infos) {
            auto referrer = queryValidPathId(*state, i.path);
            for (auto & j : i.references)
                state->stmtAddReference.use()(referrer)(queryValidPathId(*state, j)).exec();
        }

        /* Check that the derivation outputs are correct.  We can't do
           this in addValidPath() above, because the references might
           not be valid yet. */
        for (auto & i : infos)
            if (i.path.isDerivation()) {
                // FIXME: inefficient; we already loaded the derivation in addValidPath().
                checkDerivationOutputs(i.path,
                    readDerivation(*this, realStoreDir + "/" + std::string(i.path.to_string())));
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
void LocalStore::invalidatePath(State & state, const StorePath & path)
{
    debug("invalidating path '%s'", printStorePath(path));

    state.stmtInvalidatePath.use()(printStorePath(path)).exec();

    /* Note that the foreign key constraints on the Refs table take
       care of deleting the references entries for `path'. */

    {
        auto state_(Store::state.lock());
        state_->pathInfoCache.erase(storePathToHash(printStorePath(path)));
    }
}


const PublicKeys & LocalStore::getPublicKeys()
{
    auto state(_state.lock());
    if (!state->publicKeys)
        state->publicKeys = std::make_unique<PublicKeys>(getDefaultPublicKeys());
    return *state->publicKeys;
}


void LocalStore::addToStore(const ValidPathInfo & info, Source & source,
    RepairFlag repair, CheckSigsFlag checkSigs, std::shared_ptr<FSAccessor> accessor)
{
    if (!info.narHash)
        throw Error("cannot add path '%s' because it lacks a hash", printStorePath(info.path));

    if (requireSigs && checkSigs && !info.checkSignatures(*this, getPublicKeys()))
        throw Error("cannot add path '%s' because it lacks a valid signature", printStorePath(info.path));

    addTempRoot(info.path);

    if (repair || !isValidPath(info.path)) {

        PathLocks outputLock;

        Path realPath = realStoreDir + "/" + std::string(info.path.to_string());

        /* Lock the output path.  But don't lock if we're being called
           from a build hook (whose parent process already acquired a
           lock on this path). */
        if (!locksHeld.count(printStorePath(info.path)))
            outputLock.lockPaths({realPath});

        if (repair || !isValidPath(info.path)) {

            deletePath(realPath);

            if (info.ca != "" &&
                !((hasPrefix(info.ca, "text:") && !info.references.count(info.path))
                    || info.references.empty()))
                settings.requireExperimentalFeature("ca-references");

            /* While restoring the path from the NAR, compute the hash
               of the NAR. */
            std::unique_ptr<AbstractHashSink> hashSink;
            if (info.ca == "" || !info.references.count(info.path))
                hashSink = std::make_unique<HashSink>(htSHA256);
            else
                hashSink = std::make_unique<HashModuloSink>(htSHA256, storePathToHash(printStorePath(info.path)));

            LambdaSource wrapperSource([&](unsigned char * data, size_t len) -> size_t {
                size_t n = source.read(data, len);
                (*hashSink)(data, n);
                return n;
            });

            restorePath(realPath, wrapperSource);

            auto hashResult = hashSink->finish();

            if (hashResult.first != info.narHash)
                throw Error("hash mismatch importing path '%s';\n  wanted: %s\n  got:    %s",
                    printStorePath(info.path), info.narHash.to_string(), hashResult.first.to_string());

            if (hashResult.second != info.narSize)
                throw Error("size mismatch importing path '%s';\n  wanted: %s\n  got:   %s",
                    printStorePath(info.path), info.narSize, hashResult.second);

            autoGC();

            canonicalisePathMetaData(realPath, -1);

            optimisePath(realPath); // FIXME: combine with hashPath()

            registerValidPath(info);
        }

        outputLock.setDeletion(true);
    }
}


StorePath LocalStore::addToStoreFromDump(const string & dump, const string & name,
    bool recursive, HashType hashAlgo, RepairFlag repair)
{
    Hash h = hashString(hashAlgo, dump);

    auto dstPath = makeFixedOutputPath(recursive, h, name);

    addTempRoot(dstPath);

    if (repair || !isValidPath(dstPath)) {

        /* The first check above is an optimisation to prevent
           unnecessary lock acquisition. */

        Path realPath = realStoreDir + "/";
        realPath += dstPath.to_string();

        PathLocks outputLock({realPath});

        if (repair || !isValidPath(dstPath)) {

            deletePath(realPath);

            autoGC();

            if (recursive) {
                StringSource source(dump);
                restorePath(realPath, source);
            } else
                writeFile(realPath, dump);

            canonicalisePathMetaData(realPath, -1);

            /* Register the SHA-256 hash of the NAR serialisation of
               the path in the database.  We may just have computed it
               above (if called with recursive == true and hashAlgo ==
               sha256); otherwise, compute it here. */
            HashResult hash;
            if (recursive) {
                hash.first = hashAlgo == htSHA256 ? h : hashString(htSHA256, dump);
                hash.second = dump.size();
            } else
                hash = hashPath(htSHA256, realPath);

            optimisePath(realPath); // FIXME: combine with hashPath()

            ValidPathInfo info(dstPath.clone());
            info.narHash = hash.first;
            info.narSize = hash.second;
            info.ca = makeFixedOutputCA(recursive, h);
            registerValidPath(info);
        }

        outputLock.setDeletion(true);
    }

    return dstPath;
}


StorePath LocalStore::addToStore(const string & name, const Path & _srcPath,
    bool recursive, HashType hashAlgo, PathFilter & filter, RepairFlag repair)
{
    Path srcPath(absPath(_srcPath));

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


StorePath LocalStore::addTextToStore(const string & name, const string & s,
    const StorePathSet & references, RepairFlag repair)
{
    auto hash = hashString(htSHA256, s);
    auto dstPath = makeTextPath(name, hash, references);

    addTempRoot(dstPath);

    if (repair || !isValidPath(dstPath)) {

        Path realPath = realStoreDir + "/";
        realPath += dstPath.to_string();

        PathLocks outputLock({realPath});

        if (repair || !isValidPath(dstPath)) {

            deletePath(realPath);

            autoGC();

            writeFile(realPath, s);

            canonicalisePathMetaData(realPath, -1);

            StringSink sink;
            dumpString(s, sink);
            auto narHash = hashString(htSHA256, *sink.s);

            optimisePath(realPath);

            ValidPathInfo info(dstPath.clone());
            info.narHash = narHash;
            info.narSize = sink.s->size();
            info.references = cloneStorePathSet(references);
            info.ca = "text:" + hash.to_string();
            registerValidPath(info);
        }

        outputLock.setDeletion(true);
    }

    return dstPath;
}


/* Create a temporary directory in the store that won't be
   garbage-collected. */
Path LocalStore::createTempDirInStore()
{
    Path tmpDir;
    do {
        /* There is a slight possibility that `tmpDir' gets deleted by
           the GC between createTempDir() and addTempRoot(), so repeat
           until `tmpDir' exists. */
        tmpDir = createTempDir(realStoreDir);
        addTempRoot(parseStorePath(tmpDir));
    } while (!pathExists(tmpDir));
    return tmpDir;
}


void LocalStore::invalidatePathChecked(const StorePath & path)
{
    retrySQLite<void>([&]() {
        auto state(_state.lock());

        SQLiteTxn txn(state->db);

        if (isValidPath_(*state, path)) {
            StorePathSet referrers; queryReferrers(*state, path, referrers);
            referrers.erase(path); /* ignore self-references */
            if (!referrers.empty())
                throw PathInUse("cannot delete path '%s' because it is in use by %s",
                    printStorePath(path), showPaths(referrers));
            invalidatePath(*state, path);
        }

        txn.commit();
    });
}


bool LocalStore::verifyStore(bool checkContents, RepairFlag repair)
{
    printError(format("reading the Nix store..."));

    bool errors = false;

    /* Acquire the global GC lock to get a consistent snapshot of
       existing and valid paths. */
    AutoCloseFD fdGCLock = openGCLock(ltWrite);

    StringSet store;
    for (auto & i : readDirectory(realStoreDir)) store.insert(i.name);

    /* Check whether all valid paths actually exist. */
    printInfo("checking path existence...");

    StorePathSet validPaths;
    PathSet done;

    fdGCLock = -1;

    for (auto & i : queryAllValidPaths())
        verifyPath(printStorePath(i), store, done, validPaths, repair, errors);

    /* Optionally, check the content hashes (slow). */
    if (checkContents) {

        printInfo("checking link hashes...");

        for (auto & link : readDirectory(linksDir)) {
            printMsg(lvlTalkative, "checking contents of '%s'", link.name);
            Path linkPath = linksDir + "/" + link.name;
            string hash = hashPath(htSHA256, linkPath).first.to_string(Base32, false);
            if (hash != link.name) {
                printError(
                    "link '%s' was modified! expected hash '%s', got '%s'",
                    linkPath, link.name, hash);
                if (repair) {
                    if (unlink(linkPath.c_str()) == 0)
                        printError("removed link '%s'", linkPath);
                    else
                        throw SysError("removing corrupt link '%s'", linkPath);
                } else {
                    errors = true;
                }
            }
        }

        printInfo("checking store hashes...");

        Hash nullHash(htSHA256);

        for (auto & i : validPaths) {
            try {
                auto info = std::const_pointer_cast<ValidPathInfo>(std::shared_ptr<const ValidPathInfo>(queryPathInfo(i)));

                /* Check the content hash (optionally - slow). */
                printMsg(lvlTalkative, "checking contents of '%s'", printStorePath(i));

                std::unique_ptr<AbstractHashSink> hashSink;
                if (info->ca == "" || !info->references.count(info->path))
                    hashSink = std::make_unique<HashSink>(info->narHash.type);
                else
                    hashSink = std::make_unique<HashModuloSink>(info->narHash.type, storePathToHash(printStorePath(info->path)));

                dumpPath(Store::toRealPath(i), *hashSink);
                auto current = hashSink->finish();

                if (info->narHash != nullHash && info->narHash != current.first) {
                    printError("path '%s' was modified! expected hash '%s', got '%s'",
                        printStorePath(i), info->narHash.to_string(), current.first.to_string());
                    if (repair) repairPath(i); else errors = true;
                } else {

                    bool update = false;

                    /* Fill in missing hashes. */
                    if (info->narHash == nullHash) {
                        printError("fixing missing hash on '%s'", printStorePath(i));
                        info->narHash = current.first;
                        update = true;
                    }

                    /* Fill in missing narSize fields (from old stores). */
                    if (info->narSize == 0) {
                        printError("updating size field on '%s' to %s", printStorePath(i), current.second);
                        info->narSize = current.second;
                        update = true;
                    }

                    if (update) {
                        auto state(_state.lock());
                        updatePathInfo(*state, *info);
                    }

                }

            } catch (Error & e) {
                /* It's possible that the path got GC'ed, so ignore
                   errors on invalid paths. */
                if (isValidPath(i))
                    printError("error: %s", e.msg());
                else
                    warn(e.msg());
                errors = true;
            }
        }
    }

    return errors;
}


void LocalStore::verifyPath(const Path & pathS, const StringSet & store,
    PathSet & done, StorePathSet & validPaths, RepairFlag repair, bool & errors)
{
    checkInterrupt();

    if (!done.insert(pathS).second) return;

    if (!isStorePath(pathS)) {
        printError("path '%s' is not in the Nix store", pathS);
        return;
    }

    auto path = parseStorePath(pathS);

    if (!store.count(std::string(path.to_string()))) {
        /* Check any referrers first.  If we can invalidate them
           first, then we can invalidate this path as well. */
        bool canInvalidate = true;
        StorePathSet referrers; queryReferrers(path, referrers);
        for (auto & i : referrers)
            if (i != path) {
                verifyPath(printStorePath(i), store, done, validPaths, repair, errors);
                if (validPaths.count(i))
                    canInvalidate = false;
            }

        if (canInvalidate) {
            printError("path '%s' disappeared, removing from database...", pathS);
            auto state(_state.lock());
            invalidatePath(*state, path);
        } else {
            printError("path '%s' disappeared, but it still has valid referrers!", pathS);
            if (repair)
                try {
                    repairPath(path);
                } catch (Error & e) {
                    warn(e.msg());
                    errors = true;
                }
            else errors = true;
        }

        return;
    }

    validPaths.insert(std::move(path));
}


unsigned int LocalStore::getProtocol()
{
    return PROTOCOL_VERSION;
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
    AutoCloseFD fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd == -1) {
        if (errno == ELOOP) return; // it's a symlink
        throw SysError(format("opening file '%1%'") % path);
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
    printError("removing immutable bits from the Nix store (this may take a while)...");
    makeMutable(realStoreDir);
}

#else

void LocalStore::upgradeStore7()
{
}

#endif


void LocalStore::vacuumDB()
{
    auto state(_state.lock());
    state->db.exec("vacuum");
}


void LocalStore::addSignatures(const StorePath & storePath, const StringSet & sigs)
{
    retrySQLite<void>([&]() {
        auto state(_state.lock());

        SQLiteTxn txn(state->db);

        auto info = std::const_pointer_cast<ValidPathInfo>(std::shared_ptr<const ValidPathInfo>(queryPathInfo(storePath)));

        info->sigs.insert(sigs.begin(), sigs.end());

        updatePathInfo(*state, *info);

        txn.commit();
    });
}


void LocalStore::signPathInfo(ValidPathInfo & info)
{
    // FIXME: keep secret keys in memory.

    auto secretKeyFiles = settings.secretKeyFiles;

    for (auto & secretKeyFile : secretKeyFiles.get()) {
        SecretKey secretKey(readFile(secretKeyFile));
        info.sign(*this, secretKey);
    }
}


void LocalStore::createUser(const std::string & userName, uid_t userId)
{
    for (auto & dir : {
        fmt("%s/profiles/per-user/%s", stateDir, userName),
        fmt("%s/gcroots/per-user/%s", stateDir, userName)
    }) {
        createDirs(dir);
        if (chmod(dir.c_str(), 0755) == -1)
            throw SysError("changing permissions of directory '%s'", dir);
        if (chown(dir.c_str(), userId, getgid()) == -1)
            throw SysError("changing owner of directory '%s'", dir);
    }
}


}
