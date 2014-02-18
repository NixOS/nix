#include "globals.hh"
#include "misc.hh"
#include "local-store.hh"

#include <boost/shared_ptr.hpp>

#include <functional>
#include <queue>
#include <algorithm>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>


namespace nix {


static string gcLockName = "gc.lock";
static string tempRootsDir = "temproots";
static string gcRootsDir = "gcroots";


/* Acquire the global GC lock.  This is used to prevent new Nix
   processes from starting after the temporary root files have been
   read.  To be precise: when they try to create a new temporary root
   file, they will block until the garbage collector has finished /
   yielded the GC lock. */
int LocalStore::openGCLock(LockType lockType)
{
    Path fnGCLock = (format("%1%/%2%")
        % settings.nixStateDir % gcLockName).str();

    debug(format("acquiring global GC lock `%1%'") % fnGCLock);

    AutoCloseFD fdGCLock = open(fnGCLock.c_str(), O_RDWR | O_CREAT, 0600);
    if (fdGCLock == -1)
        throw SysError(format("opening global GC lock `%1%'") % fnGCLock);
    closeOnExec(fdGCLock);

    if (!lockFile(fdGCLock, lockType, false)) {
        printMsg(lvlError, format("waiting for the big garbage collector lock..."));
        lockFile(fdGCLock, lockType, true);
    }

    /* !!! Restrict read permission on the GC root.  Otherwise any
       process that can open the file for reading can DoS the
       collector. */

    return fdGCLock.borrow();
}


void createSymlink(const Path & link, const Path & target)
{
    /* Create directories up to `gcRoot'. */
    createDirs(dirOf(link));

    /* Create the new symlink. */
    Path tempLink = (format("%1%.tmp-%2%-%3%")
        % link % getpid() % rand()).str();
    if (symlink(target.c_str(), tempLink.c_str()) == -1)
        throw SysError(format("symlinking `%1%' to `%2%'")
            % tempLink % target);

    /* Atomically replace the old one. */
    if (rename(tempLink.c_str(), link.c_str()) == -1)
        throw SysError(format("cannot rename `%1%' to `%2%'")
            % tempLink % link);
}


void LocalStore::syncWithGC()
{
    AutoCloseFD fdGCLock = openGCLock(ltRead);
}


void LocalStore::addIndirectRoot(const Path & path)
{
    string hash = printHash32(hashString(htSHA1, path));
    Path realRoot = canonPath((format("%1%/%2%/auto/%3%")
        % settings.nixStateDir % gcRootsDir % hash).str());
    createSymlink(realRoot, path);
}


Path addPermRoot(StoreAPI & store, const Path & _storePath,
    const Path & _gcRoot, bool indirect, bool allowOutsideRootsDir)
{
    Path storePath(canonPath(_storePath));
    Path gcRoot(canonPath(_gcRoot));
    assertStorePath(storePath);

    if (isInStore(gcRoot))
        throw Error(format(
                "creating a garbage collector root (%1%) in the Nix store is forbidden "
                "(are you running nix-build inside the store?)") % gcRoot);

    if (indirect) {
        /* Don't clobber the the link if it already exists and doesn't
           point to the Nix store. */
        if (pathExists(gcRoot) && (!isLink(gcRoot) || !isInStore(readLink(gcRoot))))
            throw Error(format("cannot create symlink `%1%'; already exists") % gcRoot);
        createSymlink(gcRoot, storePath);
        store.addIndirectRoot(gcRoot);
    }

    else {
        if (!allowOutsideRootsDir) {
            Path rootsDir = canonPath((format("%1%/%2%") % settings.nixStateDir % gcRootsDir).str());

            if (string(gcRoot, 0, rootsDir.size() + 1) != rootsDir + "/")
                throw Error(format(
                    "path `%1%' is not a valid garbage collector root; "
                    "it's not in the directory `%2%'")
                    % gcRoot % rootsDir);
        }

        createSymlink(gcRoot, storePath);
    }

    /* Check that the root can be found by the garbage collector.
       !!! This can be very slow on machines that have many roots.
       Instead of reading all the roots, it would be more efficient to
       check if the root is in a directory in or linked from the
       gcroots directory. */
    if (settings.checkRootReachability) {
        Roots roots = store.findRoots();
        if (roots.find(gcRoot) == roots.end())
            printMsg(lvlError,
                format(
                    "warning: `%1%' is not in a directory where the garbage collector looks for roots; "
                    "therefore, `%2%' might be removed by the garbage collector")
                % gcRoot % storePath);
    }

    /* Grab the global GC root, causing us to block while a GC is in
       progress.  This prevents the set of permanent roots from
       increasing while a GC is in progress. */
    store.syncWithGC();

    return gcRoot;
}


/* The directory in which we store our temporary roots files. */
static Path dnTempRoots;
static AutoCloseFD fdTempRootsDirLock;

int LocalStore::openTempRootsFile(const string & name) {
    assert(name != "lock");
    /* Create the temporary roots directory for this process. */
    if (fdTempRootsDirLock == -1) {
        Path dir = (format("%1%/%2%") % settings.nixStateDir % tempRootsDir).str();
        createDirs(dir);

        dnTempRoots = (format("%1%/%2%")
            % dir % getpid()).str();

        Path fnTempRootsDirLock = (format("%1%/lock") % dnTempRoots).str();

        AutoCloseFD fdGCLock = openGCLock(ltRead);

        if (pathExists(dnTempRoots))
            /* It *must* be stale, since there can be no two
               processes with the same pid. Since we're in the gclock,
               we're the only process who can be deleting this right now,
               so use the non-ENOENT friendly deletePath instead
               of removeDir */
            deletePath(dnTempRoots);

        if (mkdir(dnTempRoots.c_str(), 0700) == -1)
            throw SysError("creating temporary roots directory");

        fdTempRootsDirLock = openLockFile(fnTempRootsDirLock, true);

        /* We need to take this lock before releasing the gc lock so
           that the gc doesn't delete the directory */
        debug(format("acquiring read lock on `%1%/lock'") % dnTempRoots);
        lockFile(fdTempRootsDirLock, ltRead, true);

        fdGCLock.close();
    }

    /* If the file already exists, just return it */
    Path fn = (format("%1%/%2%") % dnTempRoots % name).str();
    AutoCloseFD res = openLockFile(fn, false);
    if (res == -1) {
        /* file doesn't exist yet */
        /* Upgrade the dir lock to a write lock.  This will cause us to block
           if the garbage collector is holding our lock. */
        debug(format("acquiring write lock on `%1%/lock'") % dnTempRoots);
        lockFile(fdTempRootsDirLock, ltWrite, true);

        res = openLockFile(fn, true);

        /* Downgrade to a read lock. */
        debug(format("downgrading to read lock on `%1%/lock'") % dnTempRoots);
        lockFile(fdTempRootsDirLock, ltRead, true);
    }
    return res.borrow();
}


void LocalStore::deleteTempRootsFile(const string & name) {
    Path fn = (format("%1%/%2%") % dnTempRoots % name).str();
    /* If the file doesn't exist, noop */
    AutoCloseFD fd = openLockFile(fn, false);
    if (fd != -1) {
        /* If we delete between the gc opening the file and it
           finishing the read, it will get an error. So take the
           write lock */
        lockFile(fd, ltWrite, true);
        deletePath(fn);
    }
    /* Lock released automatically */
}


void LocalStore::addTempRoot(const Path & path)
{
    static AutoCloseFD fdTempRootsMain = openTempRootsFile("main");

    /* Get a write lock on the main temp roots file so we don't write
       to it after the gc has already started reading from it but before
       gc has finished */
    debug(format("acquiring write lock on `%1%/main'") % dnTempRoots);
    lockFile(fdTempRootsMain, ltWrite, true);

    string s = path + '\0';
    writeFull(fdTempRootsMain, (const unsigned char *) s.data(), s.size());

    debug(format("releasing write lock on `%1%/main'") % dnTempRoots);
    lockFile(fdTempRootsMain, ltNone, true);
}


void removeDir(const Path & dir) {
    /* First delete all files in the directory */
    Paths files;
    try {
        files = readDirectory(dir);
    } catch (SysError e) {
        /* OK if it's already deleted */
        if (e.errNo != ENOENT)
            throw;
        return;
    }
    foreach (Paths::iterator, i, files) {
        Path file = (format("%1%/%2%") % dir % *i).str();
        if (unlink(file.c_str()) == -1)
            /* OK if it's already deleted */
            if (errno != ENOENT)
                throw SysError(format("unlinking %1%") % file);
    }
    if (rmdir(dir.c_str()) == -1)
        /* OK if it's already deleted */
        if (errno != ENOENT)
            throw SysError(format("removing %1%") % dir);
}


void removeTempRoots()
{
    if (fdTempRootsDirLock != -1) {
        fdTempRootsDirLock.close();
        removeDir(dnTempRoots);
    }
}


/* Automatically clean up the temporary roots file when we exit. */
struct RemoveTempRoots
{
    ~RemoveTempRoots()
    {
        removeTempRoots();
    }
};

static RemoveTempRoots autoRemoveTempRoots __attribute__((unused));


typedef boost::shared_ptr<AutoCloseFD> FDPtr;
typedef list<FDPtr> FDs;


static void readTempRoots(PathSet & tempRoots, FDs & fds)
{
    /* Read the `temproots' directory for per-process temporary root
       directories. */
    Strings tempRootDirs = readDirectory(
        (format("%1%/%2%") % settings.nixStateDir % tempRootsDir).str());

    foreach (Strings::iterator, i, tempRootDirs) {
        Path dir = (format("%1%/%2%/%3%") % settings.nixStateDir % tempRootsDir % *i).str();
        Path lock = (format("%1%/lock") % dir).str();

        debug(format("reading temporary roots directory lock `%1%'") % lock);
        FDPtr fd(new AutoCloseFD(open(lock.c_str(), O_RDWR)));
        if (*fd == -1) {
            /* It's okay if the directory lock has disappeared. */
            if (errno == ENOENT) {
                removeDir(dir);
                continue;
            }
            throw SysError(format("opening temporary roots directory lock `%1%'") % lock);
        }

        /* Try to acquire a write lock without blocking.  This can
           only succeed if the owning process has died.  In that case
           we don't care about its temporary roots. */
        if (lockFile(*fd, ltWrite, false)) {
            printMsg(lvlError, format("removing stale temporary roots directory `%1%'") % dir);
            removeDir(dir);
            continue;
        }

        /* Acquire a read lock.  This will prevent the owning process
           from upgrading to a write lock, therefore it will block in
           addTempRoot(). */
        debug(format("waiting for read lock on `%1%'") % lock);
        lockFile(*fd, ltRead, true);

        /* Each file (except "lock") in this directory contains roots */
        Strings files = readDirectory(dir);
        foreach (Strings::iterator, j, files) {
            if (*j == "lock")
                continue;
            Path root = (format("%1%/%2%") % dir % *j).str();

            debug(format("reading temporary root file `%1%'") % root);
            FDPtr fdRoot(new AutoCloseFD(open(root.c_str(), O_RDONLY)));
            if (*fdRoot == -1) {
                /* It's okay if the file has disappeared. */
                if (errno == ENOENT) continue;
                throw SysError(format("opening temporary roots file `%1%'") % root);
            }

            /* Acquire a read lock on this root. Will cause owning process
               to block in addTempRoot or when reporting recursive paths
               until gc is done */
            debug(format("waiting for read lock on `%1%'") % root);
            lockFile(*fdRoot, ltRead, true);

            /* Read the entire file. */
            string contents = readFile(*fdRoot);

            /* Extract the roots. */
            string::size_type pos = 0, end;

            while ((end = contents.find((char) 0, pos)) != string::npos) {
                Path r(contents, pos, end - pos);
                debug(format("got temporary root `%1%'") % r);
                assertStorePath(r);
                tempRoots.insert(r);
                pos = end + 1;
            }

            fds.push_back(fdRoot); /* keep open */
        }
        fds.push_back(fd); /* keep open */
    }
}


static void foundRoot(StoreAPI & store,
    const Path & path, const Path & target, Roots & roots)
{
    Path storePath = toStorePath(target);
    if (store.isValidPath(storePath))
        roots[path] = storePath;
    else
        printMsg(lvlInfo, format("skipping invalid root from `%1%' to `%2%'") % path % storePath);
}


static void findRoots(StoreAPI & store, const Path & path, Roots & roots)
{
    try {

        struct stat st = lstat(path);

        if (S_ISDIR(st.st_mode)) {
            Strings names = readDirectory(path);
            foreach (Strings::iterator, i, names)
                findRoots(store, path + "/" + *i, roots);
        }

        else if (S_ISLNK(st.st_mode)) {
            Path target = readLink(path);
            if (isInStore(target))
                foundRoot(store, path, target, roots);

            /* Handle indirect roots. */
            else {
                target = absPath(target, dirOf(path));
                if (!pathExists(target)) {
                    if (isInDir(path, settings.nixStateDir + "/" + gcRootsDir + "/auto")) {
                        printMsg(lvlInfo, format("removing stale link from `%1%' to `%2%'") % path % target);
                        unlink(path.c_str());
                    }
                } else {
                    struct stat st2 = lstat(target);
                    if (!S_ISLNK(st2.st_mode)) return;
                    Path target2 = readLink(target);
                    if (isInStore(target2)) foundRoot(store, target, target2, roots);
                }
            }
        }

    }

    catch (SysError & e) {
        /* We only ignore permanent failures. */
        if (e.errNo == EACCES || e.errNo == ENOENT || e.errNo == ENOTDIR)
            printMsg(lvlInfo, format("cannot read potential root `%1%'") % path);
        else
            throw;
    }
}


Roots LocalStore::findRoots()
{
    Roots roots;

    /* Process direct roots in {gcroots,manifests,profiles}. */
    nix::findRoots(*this, settings.nixStateDir + "/" + gcRootsDir, roots);
    nix::findRoots(*this, settings.nixStateDir + "/manifests", roots);
    nix::findRoots(*this, settings.nixStateDir + "/profiles", roots);

    return roots;
}


static void addAdditionalRoots(StoreAPI & store, PathSet & roots)
{
    Path rootFinder = getEnv("NIX_ROOT_FINDER",
        settings.nixLibexecDir + "/nix/find-runtime-roots.pl");

    if (rootFinder.empty()) return;

    debug(format("executing `%1%' to find additional roots") % rootFinder);

    string result = runProgram(rootFinder);

    StringSet paths = tokenizeString<StringSet>(result, "\n");

    foreach (StringSet::iterator, i, paths) {
        if (isInStore(*i)) {
            Path path = toStorePath(*i);
            if (roots.find(path) == roots.end() && store.isValidPath(path)) {
                debug(format("got additional root `%1%'") % path);
                roots.insert(path);
            }
        }
    }
}


struct GCLimitReached { };


struct LocalStore::GCState
{
    GCOptions options;
    GCResults & results;
    PathSet roots;
    PathSet tempRoots;
    PathSet dead;
    PathSet alive;
    bool gcKeepOutputs;
    bool gcKeepDerivations;
    unsigned long long bytesInvalidated;
    Path trashDir;
    bool shouldDelete;
    GCState(GCResults & results_) : results(results_), bytesInvalidated(0) { }
};


bool LocalStore::isActiveTempFile(const GCState & state,
    const Path & path, const string & suffix)
{
    return hasSuffix(path, suffix)
        && state.tempRoots.find(string(path, 0, path.size() - suffix.size())) != state.tempRoots.end();
}


void LocalStore::deleteGarbage(GCState & state, const Path & path)
{
    unsigned long long bytesFreed;
    deletePath(path, bytesFreed);
    state.results.bytesFreed += bytesFreed;
}


void LocalStore::deletePathRecursive(GCState & state, const Path & path)
{
    checkInterrupt();

    unsigned long long size = 0;

    if (isValidPath(path)) {
        PathSet referrers;
        queryReferrers(path, referrers);
        foreach (PathSet::iterator, i, referrers)
            if (*i != path) deletePathRecursive(state, *i);
        size = queryPathInfo(path).narSize;
        invalidatePathChecked(path);
    }

    struct stat st;
    if (lstat(path.c_str(), &st)) {
        if (errno == ENOENT) return;
        throw SysError(format("getting status of %1%") % path);
    }

    printMsg(lvlInfo, format("deleting `%1%'") % path);

    state.results.paths.insert(path);

    /* If the path is not a regular file or symlink, move it to the
       trash directory.  The move is to ensure that later (when we're
       not holding the global GC lock) we can delete the path without
       being afraid that the path has become alive again.  Otherwise
       delete it right away. */
    if (S_ISDIR(st.st_mode)) {
        // Estimate the amount freed using the narSize field.  FIXME:
        // if the path was not valid, need to determine the actual
        // size.
        state.bytesInvalidated += size;
        // Mac OS X cannot rename directories if they are read-only.
        if (chmod(path.c_str(), st.st_mode | S_IWUSR) == -1)
            throw SysError(format("making `%1%' writable") % path);
        Path tmp = state.trashDir + "/" + baseNameOf(path);
        if (rename(path.c_str(), tmp.c_str()))
            throw SysError(format("unable to rename `%1%' to `%2%'") % path % tmp);
    } else
        deleteGarbage(state, path);

    if (state.results.bytesFreed + state.bytesInvalidated > state.options.maxFreed) {
        printMsg(lvlInfo, format("deleted or invalidated more than %1% bytes; stopping") % state.options.maxFreed);
        throw GCLimitReached();
    }
}


bool LocalStore::canReachRoot(GCState & state, PathSet & visited, const Path & path)
{
    if (visited.find(path) != visited.end()) return false;

    if (state.alive.find(path) != state.alive.end()) {
        return true;
    }

    if (state.dead.find(path) != state.dead.end()) {
        return false;
    }

    if (state.roots.find(path) != state.roots.end()) {
        printMsg(lvlDebug, format("cannot delete `%1%' because it's a root") % path);
        state.alive.insert(path);
        return true;
    }

    visited.insert(path);

    if (!isValidPath(path)) return false;

    PathSet incoming;

    /* Don't delete this path if any of its referrers are alive. */
    queryReferrers(path, incoming);

    /* If gc-keep-derivations is set and this is a derivation, then
       don't delete the derivation if any of the outputs are alive. */
    if (state.gcKeepDerivations && isDerivation(path)) {
        PathSet outputs = queryDerivationOutputs(path);
        foreach (PathSet::iterator, i, outputs)
            if (isValidPath(*i) && queryDeriver(*i) == path)
                incoming.insert(*i);
    }

    /* If gc-keep-outputs is set, then don't delete this path if there
       are derivers of this path that are not garbage. */
    if (state.gcKeepOutputs) {
        PathSet derivers = queryValidDerivers(path);
        foreach (PathSet::iterator, i, derivers)
            incoming.insert(*i);
    }

    foreach (PathSet::iterator, i, incoming)
        if (*i != path)
            if (canReachRoot(state, visited, *i)) {
                state.alive.insert(path);
                return true;
            }

    return false;
}


void LocalStore::tryToDelete(GCState & state, const Path & path)
{
    checkInterrupt();

    if (path == linksDir || path == state.trashDir) return;

    startNest(nest, lvlDebug, format("considering whether to delete `%1%'") % path);

    if (!isValidPath(path)) {
        /* A lock file belonging to a path that we're building right
           now isn't garbage. */
        if (isActiveTempFile(state, path, ".lock")) return;

        /* Don't delete .chroot directories for derivations that are
           currently being built. */
        if (isActiveTempFile(state, path, ".chroot")) return;
    }

    PathSet visited;

    if (canReachRoot(state, visited, path)) {
        printMsg(lvlDebug, format("cannot delete `%1%' because it's still reachable") % path);
    } else {
        /* No path we visited was a root, so everything is garbage.
           But we only delete ‘path’ and its referrers here so that
           ‘nix-store --delete’ doesn't have the unexpected effect of
           recursing into derivations and outputs. */
        state.dead.insert(visited.begin(), visited.end());
        if (state.shouldDelete)
            deletePathRecursive(state, path);
    }
}


/* Unlink all files in /nix/store/.links that have a link count of 1,
   which indicates that there are no other links and so they can be
   safely deleted.  FIXME: race condition with optimisePath(): we
   might see a link count of 1 just before optimisePath() increases
   the link count. */
void LocalStore::removeUnusedLinks(const GCState & state)
{
    AutoCloseDir dir = opendir(linksDir.c_str());
    if (!dir) throw SysError(format("opening directory `%1%'") % linksDir);

    long long actualSize = 0, unsharedSize = 0;

    struct dirent * dirent;
    while (errno = 0, dirent = readdir(dir)) {
        checkInterrupt();
        string name = dirent->d_name;
        if (name == "." || name == "..") continue;
        Path path = linksDir + "/" + name;

        struct stat st;
        if (lstat(path.c_str(), &st) == -1)
            throw SysError(format("statting `%1%'") % path);

        if (st.st_nlink != 1) {
            unsigned long long size = st.st_blocks * 512ULL;
            actualSize += size;
            unsharedSize += (st.st_nlink - 1) * size;
            continue;
        }

        printMsg(lvlTalkative, format("deleting unused link `%1%'") % path);

        if (unlink(path.c_str()) == -1)
            throw SysError(format("deleting `%1%'") % path);

        state.results.bytesFreed += st.st_blocks * 512;
    }

    struct stat st;
    if (stat(linksDir.c_str(), &st) == -1)
        throw SysError(format("statting `%1%'") % linksDir);
    long long overhead = st.st_blocks * 512ULL;

    printMsg(lvlInfo, format("note: currently hard linking saves %.2f MiB")
        % ((unsharedSize - actualSize - overhead) / (1024.0 * 1024.0)));
}


void LocalStore::collectGarbage(const GCOptions & options, GCResults & results)
{
    GCState state(results);
    state.options = options;
    state.trashDir = settings.nixStore + "/trash";
    state.gcKeepOutputs = settings.gcKeepOutputs;
    state.gcKeepDerivations = settings.gcKeepDerivations;

    /* Using `--ignore-liveness' with `--delete' can have unintended
       consequences if `gc-keep-outputs' or `gc-keep-derivations' are
       true (the garbage collector will recurse into deleting the
       outputs or derivers, respectively).  So disable them. */
    if (options.action == GCOptions::gcDeleteSpecific && options.ignoreLiveness) {
        state.gcKeepOutputs = false;
        state.gcKeepDerivations = false;
    }

    state.shouldDelete = options.action == GCOptions::gcDeleteDead || options.action == GCOptions::gcDeleteSpecific;

    /* Acquire the global GC root.  This prevents
       a) New roots from being added.
       b) Processes from creating new temporary root files. */
    AutoCloseFD fdGCLock = openGCLock(ltWrite);

    /* Find the roots.  Since we've grabbed the GC lock, the set of
       permanent roots cannot increase now. */
    printMsg(lvlError, format("finding garbage collector roots..."));
    Roots rootMap = options.ignoreLiveness ? Roots() : findRoots();

    foreach (Roots::iterator, i, rootMap) state.roots.insert(i->second);

    /* Add additional roots returned by the program specified by the
       NIX_ROOT_FINDER environment variable.  This is typically used
       to add running programs to the set of roots (to prevent them
       from being garbage collected). */
    if (!options.ignoreLiveness)
        addAdditionalRoots(*this, state.roots);

    /* Read the temporary roots.  This acquires read locks on all
       per-process temporary root files.  So after this point no paths
       can be added to the set of temporary roots. */
    FDs fds;
    readTempRoots(state.tempRoots, fds);
    state.roots.insert(state.tempRoots.begin(), state.tempRoots.end());

    /* After this point the set of roots or temporary roots cannot
       increase, since we hold locks on everything.  So everything
       that is not reachable from `roots'. */

    if (state.shouldDelete) {
        if (pathExists(state.trashDir)) deleteGarbage(state, state.trashDir);
        createDirs(state.trashDir);
    }

    /* Now either delete all garbage paths, or just the specified
       paths (for gcDeleteSpecific). */

    if (options.action == GCOptions::gcDeleteSpecific) {

        foreach (PathSet::iterator, i, options.pathsToDelete) {
            assertStorePath(*i);
            tryToDelete(state, *i);
            if (state.dead.find(*i) == state.dead.end())
                throw Error(format("cannot delete path `%1%' since it is still alive") % *i);
        }

    } else if (options.maxFreed > 0) {

        if (state.shouldDelete)
            printMsg(lvlError, format("deleting garbage..."));
        else
            printMsg(lvlError, format("determining live/dead paths..."));

        try {

            AutoCloseDir dir = opendir(settings.nixStore.c_str());
            if (!dir) throw SysError(format("opening directory `%1%'") % settings.nixStore);

            /* Read the store and immediately delete all paths that
               aren't valid.  When using --max-freed etc., deleting
               invalid paths is preferred over deleting unreachable
               paths, since unreachable paths could become reachable
               again.  We don't use readDirectory() here so that GCing
               can start faster. */
            Paths entries;
            struct dirent * dirent;
            while (errno = 0, dirent = readdir(dir)) {
                checkInterrupt();
                string name = dirent->d_name;
                if (name == "." || name == "..") continue;
                Path path = settings.nixStore + "/" + name;
                if (isValidPath(path))
                    entries.push_back(path);
                else
                    tryToDelete(state, path);
            }

            dir.close();

            /* Now delete the unreachable valid paths.  Randomise the
               order in which we delete entries to make the collector
               less biased towards deleting paths that come
               alphabetically first (e.g. /nix/store/000...).  This
               matters when using --max-freed etc. */
            vector<Path> entries_(entries.begin(), entries.end());
            random_shuffle(entries_.begin(), entries_.end());

            foreach (vector<Path>::iterator, i, entries_)
                tryToDelete(state, *i);

        } catch (GCLimitReached & e) {
        }
    }

    if (state.options.action == GCOptions::gcReturnLive) {
        state.results.paths = state.alive;
        return;
    }

    if (state.options.action == GCOptions::gcReturnDead) {
        state.results.paths = state.dead;
        return;
    }

    /* Allow other processes to add to the store from here on. */
    fdGCLock.close();
    fds.clear();

    /* Delete the trash directory. */
    printMsg(lvlInfo, format("deleting `%1%'") % state.trashDir);
    deleteGarbage(state, state.trashDir);

    /* Clean up the links directory. */
    if (options.action == GCOptions::gcDeleteDead || options.action == GCOptions::gcDeleteSpecific) {
        printMsg(lvlError, format("deleting unused links..."));
        removeUnusedLinks(state);
    }

    /* While we're at it, vacuum the database. */
    if (options.action == GCOptions::gcDeleteDead) vacuumDB();
}


}
