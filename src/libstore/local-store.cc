#include "local-store.hh"
#include "globals.hh"
#include "archive.hh"
#include "pathlocks.hh"
#include "worker-protocol.hh"
#include "derivations.hh"
#include "nar-info.hh"
#include "references.hh"
#include "callback.hh"

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
    : StoreConfig(params)
    , Store(params)
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
            logError({
                .name = "'build-users-group' not found",
                .hint = hintfmt(
                    "warning: the group '%1%' specified in 'build-users-group' does not exist",
                    settings.buildUsersGroup)
            });
        else {
            struct stat st;
            if (stat(realStoreDir.c_str(), &st))
                throw SysError("getting attributes of path '%1%'", realStoreDir);

            if (st.st_uid != 0 || st.st_gid != gr->gr_gid || (st.st_mode & ~S_IFMT) != perm) {
                if (chown(realStoreDir.c_str(), 0, gr->gr_gid) == -1)
                    throw SysError("changing ownership of path '%1%'", realStoreDir);
                if (chmod(realStoreDir.c_str(), perm) == -1)
                    throw SysError("changing permissions on path '%1%'", realStoreDir);
            }
        }
    }

    /* Ensure that the store and its parents are not symlinks. */
    if (!settings.allowSymlinkedStore) {
        Path path = realStoreDir;
        struct stat st;
        while (path != "/") {
            st = lstat(path);
            if (S_ISLNK(st.st_mode))
                throw Error(
                        "the path '%1%' is a symlink; "
                        "this is not allowed for the Nix store and its parent directories",
                        path);
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
        printInfo("waiting for the big Nix store lock...");
        lockFile(globalLock.get(), ltRead, true);
    }

    /* Check the current database schema and if necessary do an
       upgrade.  */
    int curSchema = getSchema();
    if (curSchema > nixSchemaVersion)
        throw Error("current Nix store schema is version %1%, but I only support %2%",
             curSchema, nixSchemaVersion);

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
            printInfo("waiting for exclusive access to the Nix store...");
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
        printInfo("waiting for auto-GC to finish on exit...");
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
            throw Error("'%1%' is corrupt", schemaPath);
    }
    return curSchema;
}


void LocalStore::openDB(State & state, bool create)
{
    if (access(dbDir.c_str(), R_OK | W_OK))
        throw SysError("Nix database directory '%1%' is not writable", dbDir);

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
            throw SysError("remounting %1% writable", realStoreDir);
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
                throw SysError("changing mode of '%1%' to %2$o", path, mode);
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
            throw SysError("changing modification time of '%1%'", path);
    }
}


void canonicaliseTimestampAndPermissions(const Path & path)
{
    canonicaliseTimestampAndPermissions(path, lstat(path));
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
            throw SysError("clearing flags of path '%1%'", path);
    }
#endif

    auto st = lstat(path);

    /* Really make sure that the path is of a supported type. */
    if (!(S_ISREG(st.st_mode) || S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)))
        throw Error("file '%1%' has an unsupported type", path);

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
        if (S_ISDIR(st.st_mode) || !inodesSeen.count(Inode(st.st_dev, st.st_ino)))
            throw BuildError("invalid ownership on file '%1%'", path);
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
            throw SysError("changing owner of '%1%' to %2%",
                path, geteuid());
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
    auto st = lstat(path);

    if (st.st_uid != geteuid()) {
        assert(S_ISLNK(st.st_mode));
        throw Error("wrong ownership of top-level store path '%1%'", path);
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

    auto envHasRightPath = [&](const StorePath & actual, const std::string & varName)
    {
        auto j = drv.env.find(varName);
        if (j == drv.env.end() || parseStorePath(j->second) != actual)
            throw Error("derivation '%s' has incorrect environment variable '%s', should be '%s'",
                printStorePath(drvPath), varName, printStorePath(actual));
    };


    // Don't need the answer, but do this anyways to assert is proper
    // combination. The code below is more general and naturally allows
    // combinations that are currently prohibited.
    drv.type();

    std::optional<Hash> h;
    for (auto & i : drv.outputs) {
        std::visit(overloaded {
            [&](DerivationOutputInputAddressed doia) {
                if (!h) {
                    // somewhat expensive so we do lazily
                    auto temp = hashDerivationModulo(*this, drv, true);
                    h = std::get<Hash>(temp);
                }
                StorePath recomputed = makeOutputPath(i.first, *h, drvName);
                if (doia.path != recomputed)
                    throw Error("derivation '%s' has incorrect output '%s', should be '%s'",
                        printStorePath(drvPath), printStorePath(doia.path), printStorePath(recomputed));
                envHasRightPath(doia.path, i.first);
            },
            [&](DerivationOutputCAFixed dof) {
                StorePath path = makeFixedOutputPath(dof.hash.method, dof.hash.hash, drvName);
                envHasRightPath(path, i.first);
            },
            [&](DerivationOutputCAFloating _) {
                /* Nothing to check */
            },
            [&](DerivationOutputDeferred) {
            },
        }, i.second.output);
    }
}


void LocalStore::linkDeriverToPath(const StorePath & deriver, const string & outputName, const StorePath & output)
{
    auto state(_state.lock());
    return linkDeriverToPath(*state, queryValidPathId(*state, deriver), outputName, output);
}

void LocalStore::linkDeriverToPath(State & state, uint64_t deriver, const string & outputName, const StorePath & output)
{
    retrySQLite<void>([&]() {
        state.stmtAddDerivationOutput.use()
            (deriver)
            (outputName)
            (printStorePath(output))
            .exec();
    });

}


uint64_t LocalStore::addValidPath(State & state,
    const ValidPathInfo & info, bool checkOutputs)
{
    if (info.ca.has_value() && !info.isContentAddressed(*this))
        throw Error("cannot add path '%s' to the Nix store because it claims to be content-addressed but isn't",
            printStorePath(info.path));

    state.stmtRegisterValidPath.use()
        (printStorePath(info.path))
        (info.narHash.to_string(Base16, true))
        (info.registrationTime == 0 ? time(0) : info.registrationTime)
        (info.deriver ? printStorePath(*info.deriver) : "", (bool) info.deriver)
        (info.narSize, info.narSize != 0)
        (info.ultimate ? 1 : 0, info.ultimate)
        (concatStringsSep(" ", info.sigs), !info.sigs.empty())
        (renderContentAddress(info.ca), (bool) info.ca)
        .exec();
    uint64_t id = state.db.getLastInsertedRowId();

    /* If this is a derivation, then store the derivation outputs in
       the database.  This is useful for the garbage collector: it can
       efficiently query whether a path is an output of some
       derivation. */
    if (info.path.isDerivation()) {
        auto drv = parseDerivation(
            *this,
            readFile(Store::toRealPath(info.path)),
            Derivation::nameFromPath(info.path));

        /* Verify that the output paths in the derivation are correct
           (i.e., follow the scheme for computing output paths from
           derivations).  Note that if this throws an error, then the
           DB transaction is rolled back, so the path validity
           registration above is undone. */
        if (checkOutputs) checkDerivationOutputs(info.path, drv);

        for (auto & i : drv.outputsAndOptPaths(*this)) {
            /* Floating CA derivations have indeterminate output paths until
               they are built, so don't register anything in that case */
            if (i.second.second)
                linkDeriverToPath(state, id, i.first, *i.second.second);
        }
    }

    {
        auto state_(Store::state.lock());
        state_->pathInfoCache.upsert(std::string(info.path.hashPart()),
            PathInfoCacheValue{ .value = std::make_shared<const ValidPathInfo>(info) });
    }

    return id;
}


void LocalStore::queryPathInfoUncached(const StorePath & path,
    Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept
{
    try {
        callback(retrySQLite<std::shared_ptr<ValidPathInfo>>([&]() {
            auto state(_state.lock());

            /* Get the path info. */
            auto useQueryPathInfo(state->stmtQueryPathInfo.use()(printStorePath(path)));

            if (!useQueryPathInfo.next())
                return std::shared_ptr<ValidPathInfo>();

            auto id = useQueryPathInfo.getInt(0);

            auto narHash = Hash::dummy;
            try {
                narHash = Hash::parseAnyPrefixed(useQueryPathInfo.getStr(1));
            } catch (BadHash & e) {
                throw Error("invalid-path entry for '%s': %s", printStorePath(path), e.what());
            }

            auto info = std::make_shared<ValidPathInfo>(path, narHash);

            info->id = id;

            info->registrationTime = useQueryPathInfo.getInt(2);

            auto s = (const char *) sqlite3_column_text(state->stmtQueryPathInfo, 3);
            if (s) info->deriver = parseStorePath(s);

            /* Note that narSize = NULL yields 0. */
            info->narSize = useQueryPathInfo.getInt(4);

            info->ultimate = useQueryPathInfo.getInt(5) == 1;

            s = (const char *) sqlite3_column_text(state->stmtQueryPathInfo, 6);
            if (s) info->sigs = tokenizeString<StringSet>(s, " ");

            s = (const char *) sqlite3_column_text(state->stmtQueryPathInfo, 7);
            if (s) info->ca = parseContentAddressOpt(s);

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
        (info.narHash.to_string(Base16, true))
        (info.ultimate ? 1 : 0, info.ultimate)
        (concatStringsSep(" ", info.sigs), !info.sigs.empty())
        (renderContentAddress(info.ca), (bool) info.ca)
        (printStorePath(info.path))
        .exec();
}


uint64_t LocalStore::queryValidPathId(State & state, const StorePath & path)
{
    auto use(state.stmtQueryPathInfo.use()(printStorePath(path)));
    if (!use.next())
        throw InvalidPath("path '%s' is not valid", printStorePath(path));
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
        if (isValidPath(i)) res.insert(i);
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


std::map<std::string, std::optional<StorePath>> LocalStore::queryPartialDerivationOutputMap(const StorePath & path_)
{
    auto path = path_;
    std::map<std::string, std::optional<StorePath>> outputs;
    Derivation drv = readDerivation(path);
    for (auto & [outName, _] : drv.outputs) {
        outputs.insert_or_assign(outName, std::nullopt);
    }
    bool haveCached = false;
    {
        auto resolutions = drvPathResolutions.lock();
        auto resolvedPathOptIter = resolutions->find(path);
        if (resolvedPathOptIter != resolutions->end()) {
            auto & [_, resolvedPathOpt] = *resolvedPathOptIter;
            if (resolvedPathOpt)
                path = *resolvedPathOpt;
            haveCached = true;
        }
    }
    /* can't just use else-if instead of `!haveCached` because we need to unlock
       `drvPathResolutions` before it is locked in `Derivation::resolve`. */
    if (!haveCached && (drv.type() == DerivationType::CAFloating || drv.type() == DerivationType::DeferredInputAddressed)) {
        /* Try resolve drv and use that path instead. */
        auto attempt = drv.tryResolve(*this);
        if (!attempt)
            /* If we cannot resolve the derivation, we cannot have any path
               assigned so we return the map of all std::nullopts. */
            return outputs;
        /* Just compute store path */
        auto pathResolved = writeDerivation(*this, *std::move(attempt), NoRepair, true);
        /* Store in memo table. */
        /* FIXME: memo logic should not be local-store specific, should have
           wrapper-method instead. */
        drvPathResolutions.lock()->insert_or_assign(path, pathResolved);
        path = std::move(pathResolved);
    }
    return retrySQLite<std::map<std::string, std::optional<StorePath>>>([&]() {
        auto state(_state.lock());

        uint64_t drvId;
        try {
            drvId = queryValidPathId(*state, path);
        } catch (InvalidPath &) {
            /* FIXME? if the derivation doesn't exist, we cannot have a mapping
               for it. */
            return outputs;
        }

        auto useQueryDerivationOutputs {
            state->stmtQueryDerivationOutputs.use()
            (drvId)
        };

        while (useQueryDerivationOutputs.next())
            outputs.insert_or_assign(
                useQueryDerivationOutputs.getStr(0),
                parseStorePath(useQueryDerivationOutputs.getStr(1))
            );

        return outputs;
    });
}


std::optional<StorePath> LocalStore::queryPathFromHashPart(const std::string & hashPart)
{
    if (hashPart.size() != StorePath::HashLen) throw Error("invalid hash part");

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
        remaining.insert(i);

    StorePathSet res;

    for (auto & sub : getDefaultSubstituters()) {
        if (remaining.empty()) break;
        if (sub->storeDir != storeDir) continue;
        if (!sub->wantMassQuery) continue;

        auto valid = sub->queryValidPaths(remaining);

        StorePathSet remaining2;
        for (auto & path : remaining)
            if (valid.count(path))
                res.insert(path);
            else
                remaining2.insert(path);

        std::swap(remaining, remaining2);
    }

    return res;
}


void LocalStore::querySubstitutablePathInfos(const StorePathCAMap & paths, SubstitutablePathInfos & infos)
{
    if (!settings.useSubstitutes) return;
    for (auto & sub : getDefaultSubstituters()) {
        for (auto & path : paths) {
            auto subPath(path.first);

            // recompute store path so that we can use a different store root
            if (path.second) {
                subPath = makeFixedOutputPathFromCA(path.first.name(), *path.second);
                if (sub->storeDir == storeDir)
                    assert(subPath == path.first);
                if (subPath != path.first)
                    debug("replaced path '%s' with '%s' for substituter '%s'", printStorePath(path.first), sub->printStorePath(subPath), sub->getUri());
            } else if (sub->storeDir != storeDir) continue;

            debug("checking substituter '%s' for path '%s'", sub->getUri(), sub->printStorePath(subPath));
            try {
                auto info = sub->queryPathInfo(subPath);

                if (sub->storeDir != storeDir && !(info->isContentAddressed(*sub) && info->references.empty()))
                    continue;

                auto narInfo = std::dynamic_pointer_cast<const NarInfo>(
                    std::shared_ptr<const ValidPathInfo>(info));
                infos.insert_or_assign(path.first, SubstitutablePathInfo{
                    info->deriver,
                    info->references,
                    narInfo ? narInfo->fileSize : 0,
                    info->narSize});
            } catch (InvalidPath &) {
            } catch (SubstituterDisabled &) {
            } catch (Error & e) {
                if (settings.tryFallback)
                    logError(e.info());
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
            paths.insert(i.path);
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
                    parseDerivation(
                        *this,
                        readFile(Store::toRealPath(i.path)),
                        Derivation::nameFromPath(i.path)));
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
        state_->pathInfoCache.erase(std::string(path.hashPart()));
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
    RepairFlag repair, CheckSigsFlag checkSigs)
{
    if (requireSigs && checkSigs && !info.checkSignatures(*this, getPublicKeys()))
        throw Error("cannot add path '%s' because it lacks a valid signature", printStorePath(info.path));

    addTempRoot(info.path);

    if (repair || !isValidPath(info.path)) {

        PathLocks outputLock;

        auto realPath = Store::toRealPath(info.path);

        /* Lock the output path.  But don't lock if we're being called
           from a build hook (whose parent process already acquired a
           lock on this path). */
        if (!locksHeld.count(printStorePath(info.path)))
            outputLock.lockPaths({realPath});

        if (repair || !isValidPath(info.path)) {

            deletePath(realPath);

            // text hashing has long been allowed to have non-self-references because it is used for drv files.
            bool refersToSelf = info.references.count(info.path) > 0;
            if (info.ca.has_value() && !info.references.empty() && !(std::holds_alternative<TextHash>(*info.ca) && !refersToSelf))
                settings.requireExperimentalFeature("ca-references");

            /* While restoring the path from the NAR, compute the hash
               of the NAR. */
            std::unique_ptr<AbstractHashSink> hashSink;
            if (!info.ca.has_value() || !info.references.count(info.path))
                hashSink = std::make_unique<HashSink>(htSHA256);
            else
                hashSink = std::make_unique<HashModuloSink>(htSHA256, std::string(info.path.hashPart()));

            TeeSource wrapperSource { source, *hashSink };

            restorePath(realPath, wrapperSource);

            auto hashResult = hashSink->finish();

            if (hashResult.first != info.narHash)
                throw Error("hash mismatch importing path '%s';\n  specified: %s\n  got:       %s",
                    printStorePath(info.path), info.narHash.to_string(Base32, true), hashResult.first.to_string(Base32, true));

            if (hashResult.second != info.narSize)
                throw Error("size mismatch importing path '%s';\n  specified: %s\n  got:       %s",
                    printStorePath(info.path), info.narSize, hashResult.second);

            autoGC();

            canonicalisePathMetaData(realPath, -1);

            optimisePath(realPath); // FIXME: combine with hashPath()

            registerValidPath(info);
        }

        outputLock.setDeletion(true);
    }
}


StorePath LocalStore::addToStoreFromDump(Source & source0, const string & name,
    FileIngestionMethod method, HashType hashAlgo, RepairFlag repair)
{
    /* For computing the store path. */
    auto hashSink = std::make_unique<HashSink>(hashAlgo);
    TeeSource source { source0, *hashSink };

    /* Read the source path into memory, but only if it's up to
       narBufferSize bytes. If it's larger, write it to a temporary
       location in the Nix store. If the subsequently computed
       destination store path is already valid, we just delete the
       temporary path. Otherwise, we move it to the destination store
       path. */
    bool inMemory = false;

    std::string dump;

    /* Fill out buffer, and decide whether we are working strictly in
       memory based on whether we break out because the buffer is full
       or the original source is empty */
    while (dump.size() < settings.narBufferSize) {
        auto oldSize = dump.size();
        constexpr size_t chunkSize = 65536;
        auto want = std::min(chunkSize, settings.narBufferSize - oldSize);
        dump.resize(oldSize + want);
        auto got = 0;
        try {
            got = source.read((uint8_t *) dump.data() + oldSize, want);
        } catch (EndOfFile &) {
            inMemory = true;
            break;
        }
        dump.resize(oldSize + got);
    }

    std::unique_ptr<AutoDelete> delTempDir;
    Path tempPath;

    if (!inMemory) {
        /* Drain what we pulled so far, and then keep on pulling */
        StringSource dumpSource { dump };
        ChainSource bothSource { dumpSource, source };

        auto tempDir = createTempDir(realStoreDir, "add");
        delTempDir = std::make_unique<AutoDelete>(tempDir);
        tempPath = tempDir + "/x";

        if (method == FileIngestionMethod::Recursive)
            restorePath(tempPath, bothSource);
        else
            writeFile(tempPath, bothSource);

        dump.clear();
    }

    auto [hash, size] = hashSink->finish();

    auto dstPath = makeFixedOutputPath(method, hash, name);

    addTempRoot(dstPath);

    if (repair || !isValidPath(dstPath)) {

        /* The first check above is an optimisation to prevent
           unnecessary lock acquisition. */

        auto realPath = Store::toRealPath(dstPath);

        PathLocks outputLock({realPath});

        if (repair || !isValidPath(dstPath)) {

            deletePath(realPath);

            autoGC();

            if (inMemory) {
                 StringSource dumpSource { dump };
                /* Restore from the NAR in memory. */
                if (method == FileIngestionMethod::Recursive)
                    restorePath(realPath, dumpSource);
                else
                    writeFile(realPath, dumpSource);
            } else {
                /* Move the temporary path we restored above. */
                if (rename(tempPath.c_str(), realPath.c_str()))
                    throw Error("renaming '%s' to '%s'", tempPath, realPath);
            }

            /* For computing the nar hash. In recursive SHA-256 mode, this
               is the same as the store hash, so no need to do it again. */
            auto narHash = std::pair { hash, size };
            if (method != FileIngestionMethod::Recursive || hashAlgo != htSHA256) {
                HashSink narSink { htSHA256 };
                dumpPath(realPath, narSink);
                narHash = narSink.finish();
            }

            canonicalisePathMetaData(realPath, -1); // FIXME: merge into restorePath

            optimisePath(realPath);

            ValidPathInfo info { dstPath, narHash.first };
            info.narSize = narHash.second;
            info.ca = FixedOutputHash { .method = method, .hash = hash };
            registerValidPath(info);
        }

        outputLock.setDeletion(true);
    }

    return dstPath;
}


StorePath LocalStore::addTextToStore(const string & name, const string & s,
    const StorePathSet & references, RepairFlag repair)
{
    auto hash = hashString(htSHA256, s);
    auto dstPath = makeTextPath(name, hash, references);

    addTempRoot(dstPath);

    if (repair || !isValidPath(dstPath)) {

        auto realPath = Store::toRealPath(dstPath);

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

            ValidPathInfo info { dstPath, narHash };
            info.narSize = sink.s->size();
            info.references = references;
            info.ca = TextHash { .hash = hash };
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
    printInfo(format("reading the Nix store..."));

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
                logError({
                    .name = "Invalid hash",
                    .hint = hintfmt(
                        "link '%s' was modified! expected hash '%s', got '%s'",
                        linkPath, link.name, hash)
                });
                if (repair) {
                    if (unlink(linkPath.c_str()) == 0)
                        printInfo("removed link '%s'", linkPath);
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
                if (!info->ca || !info->references.count(info->path))
                    hashSink = std::make_unique<HashSink>(info->narHash.type);
                else
                    hashSink = std::make_unique<HashModuloSink>(info->narHash.type, std::string(info->path.hashPart()));

                dumpPath(Store::toRealPath(i), *hashSink);
                auto current = hashSink->finish();

                if (info->narHash != nullHash && info->narHash != current.first) {
                    logError({
                        .name = "Invalid hash - path modified",
                        .hint = hintfmt("path '%s' was modified! expected hash '%s', got '%s'",
                        printStorePath(i), info->narHash.to_string(Base32, true), current.first.to_string(Base32, true))
                    });
                    if (repair) repairPath(i); else errors = true;
                } else {

                    bool update = false;

                    /* Fill in missing hashes. */
                    if (info->narHash == nullHash) {
                        printInfo("fixing missing hash on '%s'", printStorePath(i));
                        info->narHash = current.first;
                        update = true;
                    }

                    /* Fill in missing narSize fields (from old stores). */
                    if (info->narSize == 0) {
                        printInfo("updating size field on '%s' to %s", printStorePath(i), current.second);
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
                    logError(e.info());
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
        logError({
            .name = "Nix path not found",
            .hint = hintfmt("path '%s' is not in the Nix store", pathS)
        });
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
            printInfo("path '%s' disappeared, removing from database...", pathS);
            auto state(_state.lock());
            invalidatePath(*state, path);
        } else {
            logError({
                .name = "Missing path with referrers",
                .hint = hintfmt("path '%s' disappeared, but it still has valid referrers!", pathS)
            });
            if (repair)
                try {
                    repairPath(path);
                } catch (Error & e) {
                    logWarning(e.info());
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

    auto st = lstat(path);

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
        throw SysError("opening file '%1%'", path);
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
    printInfo("removing immutable bits from the Nix store (this may take a while)...");
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
