#include "derivations.hh"
#include "globals.hh"
#include "local-store.hh"
#include "finally.hh"

#include <functional>
#include <queue>
#include <algorithm>
#include <regex>
#include <random>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <climits>

namespace nix {


static string gcLockName = "gc.lock";
static string gcRootsDir = "gcroots";


/* Acquire the global GC lock.  This is used to prevent new Nix
   processes from starting after the temporary root files have been
   read.  To be precise: when they try to create a new temporary root
   file, they will block until the garbage collector has finished /
   yielded the GC lock. */
AutoCloseFD LocalStore::openGCLock(LockType lockType)
{
    Path fnGCLock = (format("%1%/%2%")
        % stateDir % gcLockName).str();

    debug(format("acquiring global GC lock '%1%'") % fnGCLock);

    AutoCloseFD fdGCLock = open(fnGCLock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (!fdGCLock)
        throw SysError(format("opening global GC lock '%1%'") % fnGCLock);

    if (!lockFile(fdGCLock.get(), lockType, false)) {
        printError(format("waiting for the big garbage collector lock..."));
        lockFile(fdGCLock.get(), lockType, true);
    }

    /* !!! Restrict read permission on the GC root.  Otherwise any
       process that can open the file for reading can DoS the
       collector. */

    return fdGCLock;
}


static void makeSymlink(const Path & link, const Path & target)
{
    /* Create directories up to `gcRoot'. */
    createDirs(dirOf(link));

    /* Create the new symlink. */
    Path tempLink = (format("%1%.tmp-%2%-%3%")
        % link % getpid() % random()).str();
    createSymlink(target, tempLink);

    /* Atomically replace the old one. */
    if (rename(tempLink.c_str(), link.c_str()) == -1)
        throw SysError(format("cannot rename '%1%' to '%2%'")
            % tempLink % link);
}


void LocalStore::syncWithGC()
{
    AutoCloseFD fdGCLock = openGCLock(ltRead);
}


void LocalStore::addIndirectRoot(const Path & path)
{
    string hash = hashString(htSHA1, path).to_string(Base32, false);
    Path realRoot = canonPath((format("%1%/%2%/auto/%3%")
        % stateDir % gcRootsDir % hash).str());
    makeSymlink(realRoot, path);
}


Path LocalFSStore::addPermRoot(const Path & _storePath,
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
        /* Don't clobber the link if it already exists and doesn't
           point to the Nix store. */
        if (pathExists(gcRoot) && (!isLink(gcRoot) || !isInStore(readLink(gcRoot))))
            throw Error(format("cannot create symlink '%1%'; already exists") % gcRoot);
        makeSymlink(gcRoot, storePath);
        addIndirectRoot(gcRoot);
    }

    else {
        if (!allowOutsideRootsDir) {
            Path rootsDir = canonPath((format("%1%/%2%") % stateDir % gcRootsDir).str());

            if (string(gcRoot, 0, rootsDir.size() + 1) != rootsDir + "/")
                throw Error(format(
                    "path '%1%' is not a valid garbage collector root; "
                    "it's not in the directory '%2%'")
                    % gcRoot % rootsDir);
        }

        if (baseNameOf(gcRoot) == baseNameOf(storePath))
            writeFile(gcRoot, "");
        else
            makeSymlink(gcRoot, storePath);
    }

    /* Check that the root can be found by the garbage collector.
       !!! This can be very slow on machines that have many roots.
       Instead of reading all the roots, it would be more efficient to
       check if the root is in a directory in or linked from the
       gcroots directory. */
    if (settings.checkRootReachability) {
        Roots roots = findRoots(false);
        if (roots[storePath].count(gcRoot) == 0)
            printError(
                format(
                    "warning: '%1%' is not in a directory where the garbage collector looks for roots; "
                    "therefore, '%2%' might be removed by the garbage collector")
                % gcRoot % storePath);
    }

    /* Grab the global GC root, causing us to block while a GC is in
       progress.  This prevents the set of permanent roots from
       increasing while a GC is in progress. */
    syncWithGC();

    return gcRoot;
}


void LocalStore::addTempRoot(const Path & path)
{
    auto state(_state.lock());

    /* Create the temporary roots file for this process. */
    if (!state->fdTempRoots) {

        while (1) {
            AutoCloseFD fdGCLock = openGCLock(ltRead);

            if (pathExists(fnTempRoots))
                /* It *must* be stale, since there can be no two
                   processes with the same pid. */
                unlink(fnTempRoots.c_str());

            state->fdTempRoots = openLockFile(fnTempRoots, true);

            fdGCLock = -1;

            debug(format("acquiring read lock on '%1%'") % fnTempRoots);
            lockFile(state->fdTempRoots.get(), ltRead, true);

            /* Check whether the garbage collector didn't get in our
               way. */
            struct stat st;
            if (fstat(state->fdTempRoots.get(), &st) == -1)
                throw SysError(format("statting '%1%'") % fnTempRoots);
            if (st.st_size == 0) break;

            /* The garbage collector deleted this file before we could
               get a lock.  (It won't delete the file after we get a
               lock.)  Try again. */
        }

    }

    /* Upgrade the lock to a write lock.  This will cause us to block
       if the garbage collector is holding our lock. */
    debug(format("acquiring write lock on '%1%'") % fnTempRoots);
    lockFile(state->fdTempRoots.get(), ltWrite, true);

    string s = path + '\0';
    writeFull(state->fdTempRoots.get(), s);

    /* Downgrade to a read lock. */
    debug(format("downgrading to read lock on '%1%'") % fnTempRoots);
    lockFile(state->fdTempRoots.get(), ltRead, true);
}


static std::string censored = "{censored}";


void LocalStore::findTempRoots(FDs & fds, Roots & tempRoots, bool censor)
{
    /* Read the `temproots' directory for per-process temporary root
       files. */
    for (auto & i : readDirectory(tempRootsDir)) {
        Path path = tempRootsDir + "/" + i.name;

        pid_t pid = std::stoi(i.name);

        debug(format("reading temporary root file '%1%'") % path);
        FDPtr fd(new AutoCloseFD(open(path.c_str(), O_CLOEXEC | O_RDWR, 0666)));
        if (!*fd) {
            /* It's okay if the file has disappeared. */
            if (errno == ENOENT) continue;
            throw SysError(format("opening temporary roots file '%1%'") % path);
        }

        /* This should work, but doesn't, for some reason. */
        //FDPtr fd(new AutoCloseFD(openLockFile(path, false)));
        //if (*fd == -1) continue;

        /* Try to acquire a write lock without blocking.  This can
           only succeed if the owning process has died.  In that case
           we don't care about its temporary roots. */
        if (lockFile(fd->get(), ltWrite, false)) {
            printError(format("removing stale temporary roots file '%1%'") % path);
            unlink(path.c_str());
            writeFull(fd->get(), "d");
            continue;
        }

        /* Acquire a read lock.  This will prevent the owning process
           from upgrading to a write lock, therefore it will block in
           addTempRoot(). */
        debug(format("waiting for read lock on '%1%'") % path);
        lockFile(fd->get(), ltRead, true);

        /* Read the entire file. */
        string contents = readFile(fd->get());

        /* Extract the roots. */
        string::size_type pos = 0, end;

        while ((end = contents.find((char) 0, pos)) != string::npos) {
            Path root(contents, pos, end - pos);
            debug("got temporary root '%s'", root);
            assertStorePath(root);
            tempRoots[root].emplace(censor ? censored : fmt("{temp:%d}", pid));
            pos = end + 1;
        }

        fds.push_back(fd); /* keep open */
    }
}


void LocalStore::findRoots(const Path & path, unsigned char type, Roots & roots)
{
    auto foundRoot = [&](const Path & path, const Path & target) {
        Path storePath = toStorePath(target);
        if (isStorePath(storePath) && isValidPath(storePath))
            roots[storePath].emplace(path);
        else
            printInfo(format("skipping invalid root from '%1%' to '%2%'") % path % storePath);
    };

    try {

        if (type == DT_UNKNOWN)
            type = getFileType(path);

        if (type == DT_DIR) {
            for (auto & i : readDirectory(path))
                findRoots(path + "/" + i.name, i.type, roots);
        }

        else if (type == DT_LNK) {
            Path target = readLink(path);
            if (isInStore(target))
                foundRoot(path, target);

            /* Handle indirect roots. */
            else {
                target = absPath(target, dirOf(path));
                if (!pathExists(target)) {
                    if (isInDir(path, stateDir + "/" + gcRootsDir + "/auto")) {
                        printInfo(format("removing stale link from '%1%' to '%2%'") % path % target);
                        unlink(path.c_str());
                    }
                } else {
                    struct stat st2 = lstat(target);
                    if (!S_ISLNK(st2.st_mode)) return;
                    Path target2 = readLink(target);
                    if (isInStore(target2)) foundRoot(target, target2);
                }
            }
        }

        else if (type == DT_REG) {
            Path storePath = storeDir + "/" + baseNameOf(path);
            if (isStorePath(storePath) && isValidPath(storePath))
                roots[storePath].emplace(path);
        }

    }

    catch (SysError & e) {
        /* We only ignore permanent failures. */
        if (e.errNo == EACCES || e.errNo == ENOENT || e.errNo == ENOTDIR)
            printInfo(format("cannot read potential root '%1%'") % path);
        else
            throw;
    }
}


void LocalStore::findRootsNoTemp(Roots & roots, bool censor)
{
    /* Process direct roots in {gcroots,profiles}. */
    findRoots(stateDir + "/" + gcRootsDir, DT_UNKNOWN, roots);
    findRoots(stateDir + "/profiles", DT_UNKNOWN, roots);

    /* Add additional roots returned by different platforms-specific
       heuristics.  This is typically used to add running programs to
       the set of roots (to prevent them from being garbage collected). */
    findRuntimeRoots(roots, censor);
}


Roots LocalStore::findRoots(bool censor)
{
    Roots roots;
    findRootsNoTemp(roots, censor);

    FDs fds;
    findTempRoots(fds, roots, censor);

    return roots;
}

static void readProcLink(const string & file, Roots & roots)
{
    /* 64 is the starting buffer size gnu readlink uses... */
    auto bufsiz = ssize_t{64};
try_again:
    char buf[bufsiz];
    auto res = readlink(file.c_str(), buf, bufsiz);
    if (res == -1) {
        if (errno == ENOENT || errno == EACCES || errno == ESRCH)
            return;
        throw SysError("reading symlink");
    }
    if (res == bufsiz) {
        if (SSIZE_MAX / 2 < bufsiz)
            throw Error("stupidly long symlink");
        bufsiz *= 2;
        goto try_again;
    }
    if (res > 0 && buf[0] == '/')
        roots[std::string(static_cast<char *>(buf), res)]
            .emplace(file);
}

static string quoteRegexChars(const string & raw)
{
    static auto specialRegex = std::regex(R"([.^$\\*+?()\[\]{}|])");
    return std::regex_replace(raw, specialRegex, R"(\$&)");
}

static void readFileRoots(const char * path, Roots & roots)
{
    try {
        roots[readFile(path)].emplace(path);
    } catch (SysError & e) {
        if (e.errNo != ENOENT && e.errNo != EACCES)
            throw;
    }
}

void LocalStore::findRuntimeRoots(Roots & roots, bool censor)
{
    Roots unchecked;

    auto procDir = AutoCloseDir{opendir("/proc")};
    if (procDir) {
        struct dirent * ent;
        auto digitsRegex = std::regex(R"(^\d+$)");
        auto mapRegex = std::regex(R"(^\s*\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+(/\S+)\s*$)");
        auto storePathRegex = std::regex(quoteRegexChars(storeDir) + R"(/[0-9a-z]+[0-9a-zA-Z\+\-\._\?=]*)");
        while (errno = 0, ent = readdir(procDir.get())) {
            checkInterrupt();
            if (std::regex_match(ent->d_name, digitsRegex)) {
                readProcLink(fmt("/proc/%s/exe" ,ent->d_name), unchecked);
                readProcLink(fmt("/proc/%s/cwd", ent->d_name), unchecked);

                auto fdStr = fmt("/proc/%s/fd", ent->d_name);
                auto fdDir = AutoCloseDir(opendir(fdStr.c_str()));
                if (!fdDir) {
                    if (errno == ENOENT || errno == EACCES)
                        continue;
                    throw SysError(format("opening %1%") % fdStr);
                }
                struct dirent * fd_ent;
                while (errno = 0, fd_ent = readdir(fdDir.get())) {
                    if (fd_ent->d_name[0] != '.')
                        readProcLink(fmt("%s/%s", fdStr, fd_ent->d_name), unchecked);
                }
                if (errno) {
                    if (errno == ESRCH)
                        continue;
                    throw SysError(format("iterating /proc/%1%/fd") % ent->d_name);
                }
                fdDir.reset();

                try {
                    auto mapFile = fmt("/proc/%s/maps", ent->d_name);
                    auto mapLines = tokenizeString<std::vector<string>>(readFile(mapFile, true), "\n");
                    for (const auto & line : mapLines) {
                        auto match = std::smatch{};
                        if (std::regex_match(line, match, mapRegex))
                            unchecked[match[1]].emplace(mapFile);
                    }

                    auto envFile = fmt("/proc/%s/environ", ent->d_name);
                    auto envString = readFile(envFile, true);
                    auto env_end = std::sregex_iterator{};
                    for (auto i = std::sregex_iterator{envString.begin(), envString.end(), storePathRegex}; i != env_end; ++i)
                        unchecked[i->str()].emplace(envFile);
                } catch (SysError & e) {
                    if (errno == ENOENT || errno == EACCES || errno == ESRCH)
                        continue;
                    throw;
                }
            }
        }
        if (errno)
            throw SysError("iterating /proc");
    }

#if !defined(__linux__)
    // lsof is really slow on OS X. This actually causes the gc-concurrent.sh test to fail.
    // See: https://github.com/NixOS/nix/issues/3011
    // Because of this we disable lsof when running the tests.
    if (getEnv("_NIX_TEST_NO_LSOF") == "") {
        try {
            std::regex lsofRegex(R"(^n(/.*)$)");
            auto lsofLines =
                tokenizeString<std::vector<string>>(runProgram(LSOF, true, { "-n", "-w", "-F", "n" }), "\n");
            for (const auto & line : lsofLines) {
                std::smatch match;
                if (std::regex_match(line, match, lsofRegex))
                    unchecked[match[1]].emplace("{lsof}");
            }
        } catch (ExecError & e) {
            /* lsof not installed, lsof failed */
        }
    }
#endif

#if defined(__linux__)
    readFileRoots("/proc/sys/kernel/modprobe", unchecked);
    readFileRoots("/proc/sys/kernel/fbsplash", unchecked);
    readFileRoots("/proc/sys/kernel/poweroff_cmd", unchecked);
#endif

    for (auto & [target, links] : unchecked) {
        if (isInStore(target)) {
            Path path = toStorePath(target);
            if (isStorePath(path) && isValidPath(path)) {
                debug(format("got additional root '%1%'") % path);
                if (censor)
                    roots[path].insert(censored);
                else
                    roots[path].insert(links.begin(), links.end());
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
    bool moveToTrash = true;
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

    if (isStorePath(path) && isValidPath(path)) {
        PathSet referrers;
        queryReferrers(path, referrers);
        for (auto & i : referrers)
            if (i != path) deletePathRecursive(state, i);
        size = queryPathInfo(path)->narSize;
        invalidatePathChecked(path);
    }

    Path realPath = realStoreDir + "/" + baseNameOf(path);

    struct stat st;
    if (lstat(realPath.c_str(), &st)) {
        if (errno == ENOENT) return;
        throw SysError(format("getting status of %1%") % realPath);
    }

    printInfo(format("deleting '%1%'") % path);

    state.results.paths.insert(path);

    /* If the path is not a regular file or symlink, move it to the
       trash directory.  The move is to ensure that later (when we're
       not holding the global GC lock) we can delete the path without
       being afraid that the path has become alive again.  Otherwise
       delete it right away. */
    if (state.moveToTrash && S_ISDIR(st.st_mode)) {
        // Estimate the amount freed using the narSize field.  FIXME:
        // if the path was not valid, need to determine the actual
        // size.
        try {
            if (chmod(realPath.c_str(), st.st_mode | S_IWUSR) == -1)
                throw SysError(format("making '%1%' writable") % realPath);
            Path tmp = trashDir + "/" + baseNameOf(path);
            if (rename(realPath.c_str(), tmp.c_str()))
                throw SysError(format("unable to rename '%1%' to '%2%'") % realPath % tmp);
            state.bytesInvalidated += size;
        } catch (SysError & e) {
            if (e.errNo == ENOSPC) {
                printInfo(format("note: can't create move '%1%': %2%") % realPath % e.msg());
                deleteGarbage(state, realPath);
            }
        }
    } else
        deleteGarbage(state, realPath);

    if (state.results.bytesFreed + state.bytesInvalidated > state.options.maxFreed) {
        printInfo(format("deleted or invalidated more than %1% bytes; stopping") % state.options.maxFreed);
        throw GCLimitReached();
    }
}


bool LocalStore::canReachRoot(GCState & state, PathSet & visited, const Path & path)
{
    if (visited.count(path)) return false;

    if (state.alive.count(path)) return true;

    if (state.dead.count(path)) return false;

    if (state.roots.count(path)) {
        debug(format("cannot delete '%1%' because it's a root") % path);
        state.alive.insert(path);
        return true;
    }

    visited.insert(path);

    if (!isStorePath(path) || !isValidPath(path)) return false;

    PathSet incoming;

    /* Don't delete this path if any of its referrers are alive. */
    queryReferrers(path, incoming);

    /* If keep-derivations is set and this is a derivation, then
       don't delete the derivation if any of the outputs are alive. */
    if (state.gcKeepDerivations && isDerivation(path)) {
        PathSet outputs = queryDerivationOutputs(path);
        for (auto & i : outputs)
            if (isValidPath(i) && queryPathInfo(i)->deriver == path)
                incoming.insert(i);
    }

    /* If keep-outputs is set, then don't delete this path if there
       are derivers of this path that are not garbage. */
    if (state.gcKeepOutputs) {
        PathSet derivers = queryValidDerivers(path);
        for (auto & i : derivers)
            incoming.insert(i);
    }

    for (auto & i : incoming)
        if (i != path)
            if (canReachRoot(state, visited, i)) {
                state.alive.insert(path);
                return true;
            }

    return false;
}


void LocalStore::tryToDelete(GCState & state, const Path & path)
{
    checkInterrupt();

    auto realPath = realStoreDir + "/" + baseNameOf(path);
    if (realPath == linksDir || realPath == trashDir) return;

    //Activity act(*logger, lvlDebug, format("considering whether to delete '%1%'") % path);

    if (!isStorePath(path) || !isValidPath(path)) {
        /* A lock file belonging to a path that we're building right
           now isn't garbage. */
        if (isActiveTempFile(state, path, ".lock")) return;

        /* Don't delete .chroot directories for derivations that are
           currently being built. */
        if (isActiveTempFile(state, path, ".chroot")) return;

        /* Don't delete .check directories for derivations that are
           currently being built, because we may need to run
           diff-hook. */
        if (isActiveTempFile(state, path, ".check")) return;
    }

    PathSet visited;

    if (canReachRoot(state, visited, path)) {
        debug(format("cannot delete '%1%' because it's still reachable") % path);
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
    AutoCloseDir dir(opendir(linksDir.c_str()));
    if (!dir) throw SysError(format("opening directory '%1%'") % linksDir);

    long long actualSize = 0, unsharedSize = 0;

    struct dirent * dirent;
    while (errno = 0, dirent = readdir(dir.get())) {
        checkInterrupt();
        string name = dirent->d_name;
        if (name == "." || name == "..") continue;
        Path path = linksDir + "/" + name;

        struct stat st;
        if (lstat(path.c_str(), &st) == -1)
            throw SysError(format("statting '%1%'") % path);

        if (st.st_nlink != 1) {
            actualSize += st.st_size;
            unsharedSize += (st.st_nlink - 1) * st.st_size;
            continue;
        }

        printMsg(lvlTalkative, format("deleting unused link '%1%'") % path);

        if (unlink(path.c_str()) == -1)
            throw SysError(format("deleting '%1%'") % path);

        state.results.bytesFreed += st.st_size;
    }

    struct stat st;
    if (stat(linksDir.c_str(), &st) == -1)
        throw SysError(format("statting '%1%'") % linksDir);
    long long overhead = st.st_blocks * 512ULL;

    printInfo(format("note: currently hard linking saves %.2f MiB")
        % ((unsharedSize - actualSize - overhead) / (1024.0 * 1024.0)));
}


void LocalStore::collectGarbage(const GCOptions & options, GCResults & results)
{
    GCState state(results);
    state.options = options;
    state.gcKeepOutputs = settings.gcKeepOutputs;
    state.gcKeepDerivations = settings.gcKeepDerivations;

    /* Using `--ignore-liveness' with `--delete' can have unintended
       consequences if `keep-outputs' or `keep-derivations' are true
       (the garbage collector will recurse into deleting the outputs
       or derivers, respectively).  So disable them. */
    if (options.action == GCOptions::gcDeleteSpecific && options.ignoreLiveness) {
        state.gcKeepOutputs = false;
        state.gcKeepDerivations = false;
    }

    state.shouldDelete = options.action == GCOptions::gcDeleteDead || options.action == GCOptions::gcDeleteSpecific;

    if (state.shouldDelete)
        deletePath(reservedPath);

    /* Acquire the global GC root.  This prevents
       a) New roots from being added.
       b) Processes from creating new temporary root files. */
    AutoCloseFD fdGCLock = openGCLock(ltWrite);

    /* Find the roots.  Since we've grabbed the GC lock, the set of
       permanent roots cannot increase now. */
    printError(format("finding garbage collector roots..."));
    Roots rootMap;
    if (!options.ignoreLiveness)
        findRootsNoTemp(rootMap, true);

    for (auto & i : rootMap) state.roots.insert(i.first);

    /* Read the temporary roots.  This acquires read locks on all
       per-process temporary root files.  So after this point no paths
       can be added to the set of temporary roots. */
    FDs fds;
    Roots tempRoots;
    findTempRoots(fds, tempRoots, true);
    for (auto & root : tempRoots)
        state.tempRoots.insert(root.first);
    state.roots.insert(state.tempRoots.begin(), state.tempRoots.end());

    /* After this point the set of roots or temporary roots cannot
       increase, since we hold locks on everything.  So everything
       that is not reachable from `roots' is garbage. */

    if (state.shouldDelete) {
        if (pathExists(trashDir)) deleteGarbage(state, trashDir);
        try {
            createDirs(trashDir);
        } catch (SysError & e) {
            if (e.errNo == ENOSPC) {
                printInfo(format("note: can't create trash directory: %1%") % e.msg());
                state.moveToTrash = false;
            }
        }
    }

    /* Now either delete all garbage paths, or just the specified
       paths (for gcDeleteSpecific). */

    if (options.action == GCOptions::gcDeleteSpecific) {

        for (auto & i : options.pathsToDelete) {
            assertStorePath(i);
            tryToDelete(state, i);
            if (state.dead.find(i) == state.dead.end())
                throw Error(format("cannot delete path '%1%' since it is still alive") % i);
        }

    } else if (options.maxFreed > 0) {

        if (state.shouldDelete)
            printError(format("deleting garbage..."));
        else
            printError(format("determining live/dead paths..."));

        try {

            AutoCloseDir dir(opendir(realStoreDir.c_str()));
            if (!dir) throw SysError(format("opening directory '%1%'") % realStoreDir);

            /* Read the store and immediately delete all paths that
               aren't valid.  When using --max-freed etc., deleting
               invalid paths is preferred over deleting unreachable
               paths, since unreachable paths could become reachable
               again.  We don't use readDirectory() here so that GCing
               can start faster. */
            Paths entries;
            struct dirent * dirent;
            while (errno = 0, dirent = readdir(dir.get())) {
                checkInterrupt();
                string name = dirent->d_name;
                if (name == "." || name == "..") continue;
                Path path = storeDir + "/" + name;
                if (isStorePath(path) && isValidPath(path))
                    entries.push_back(path);
                else
                    tryToDelete(state, path);
            }

            dir.reset();

            /* Now delete the unreachable valid paths.  Randomise the
               order in which we delete entries to make the collector
               less biased towards deleting paths that come
               alphabetically first (e.g. /nix/store/000...).  This
               matters when using --max-freed etc. */
            vector<Path> entries_(entries.begin(), entries.end());
            std::mt19937 gen(1);
            std::shuffle(entries_.begin(), entries_.end(), gen);

            for (auto & i : entries_)
                tryToDelete(state, i);

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
    fdGCLock = -1;
    fds.clear();

    /* Delete the trash directory. */
    printInfo(format("deleting '%1%'") % trashDir);
    deleteGarbage(state, trashDir);

    /* Clean up the links directory. */
    if (options.action == GCOptions::gcDeleteDead || options.action == GCOptions::gcDeleteSpecific) {
        printError(format("deleting unused links..."));
        removeUnusedLinks(state);
    }

    /* While we're at it, vacuum the database. */
    //if (options.action == GCOptions::gcDeleteDead) vacuumDB();
}


void LocalStore::autoGC(bool sync)
{
    static auto fakeFreeSpaceFile = getEnv("_NIX_TEST_FREE_SPACE_FILE", "");

    auto getAvail = [this]() -> uint64_t {
        if (!fakeFreeSpaceFile.empty())
            return std::stoll(readFile(fakeFreeSpaceFile));

        struct statvfs st;
        if (statvfs(realStoreDir.c_str(), &st))
            throw SysError("getting filesystem info about '%s'", realStoreDir);

        return (uint64_t) st.f_bavail * st.f_bsize;
    };

    std::shared_future<void> future;

    {
        auto state(_state.lock());

        if (state->gcRunning) {
            future = state->gcFuture;
            debug("waiting for auto-GC to finish");
            goto sync;
        }

        auto now = std::chrono::steady_clock::now();

        if (now < state->lastGCCheck + std::chrono::seconds(settings.minFreeCheckInterval)) return;

        auto avail = getAvail();

        state->lastGCCheck = now;

        if (avail >= settings.minFree || avail >= settings.maxFree) return;

        if (avail > state->availAfterGC * 0.97) return;

        state->gcRunning = true;

        std::promise<void> promise;
        future = state->gcFuture = promise.get_future().share();

        std::thread([promise{std::move(promise)}, this, avail, getAvail]() mutable {

            try {

                /* Wake up any threads waiting for the auto-GC to finish. */
                Finally wakeup([&]() {
                    auto state(_state.lock());
                    state->gcRunning = false;
                    state->lastGCCheck = std::chrono::steady_clock::now();
                    promise.set_value();
                });

                GCOptions options;
                options.maxFreed = settings.maxFree - avail;

                printInfo("running auto-GC to free %d bytes", options.maxFreed);

                GCResults results;

                collectGarbage(options, results);

                _state.lock()->availAfterGC = getAvail();

            } catch (...) {
                // FIXME: we could propagate the exception to the
                // future, but we don't really care.
                ignoreException();
            }

        }).detach();
    }

 sync:
    // Wait for the future outside of the state lock.
    if (sync) future.get();
}


}
