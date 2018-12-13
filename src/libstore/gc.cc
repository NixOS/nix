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
#ifdef HAVE_STATVFS
#include <sys/statvfs.h>
#endif
#include <errno.h>
#include <fcntl.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif
#include <climits>

#ifdef _WIN32
#include <iostream>
#endif

namespace nix {


static string gcLockName = "gc.lock";
static string gcRootsDir = "gcroots";


/* Acquire the global GC lock.  This is used to prevent new Nix
   processes from starting after the temporary root files have been
   read.  To be precise: when they try to create a new temporary root
   file, they will block until the garbage collector has finished /
   yielded the GC lock. */
#ifndef _WIN32
int LocalStore::openGCLock(LockType lockType)
#else
HANDLE LocalStore::openGCLock(LockType lockType)
#endif
{
    Path fnGCLock = (format("%1%/%2%")
        % stateDir % gcLockName).str();

    debug(format("acquiring global GC lock '%1%' '%2%'") % fnGCLock % lockType);

#ifndef _WIN32
    AutoCloseFD fdGCLock = open(fnGCLock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (!fdGCLock)
        throw PosixError("opening global GC lock '%1%'", fnGCLock);
#else
    AutoCloseWindowsHandle fdGCLock = CreateFileW(pathW(fnGCLock).c_str(), GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_POSIX_SEMANTICS, NULL);
    if (!fdGCLock)
        throw WinError("opening global GC lock '%1%'", fnGCLock);
#endif

    if (!lockFile(fdGCLock.get(), lockType, false)) {
        printError(format("waiting for the big garbage collector lock..."));
        lockFile(fdGCLock.get(), lockType, true);
    }

    /* !!! Restrict read permission on the GC root.  Otherwise any
       process that can open the file for reading can DoS the
       collector. */

    return fdGCLock.release();
}


static void makeSymlink(const Path & link, const Path & target)
{
    /* Create directories up to `gcRoot'. */
    createDirs(dirOf(link));

#ifndef _WIN32
    /* Create the new symlink. */
    Path tempLink = (format("%1%.tmp-%2%-%3%")
        % link % getpid() % random()).str();
    createSymlink(target, tempLink);

    /* Atomically replace the old one. */
    if (rename(tempLink.c_str(), link.c_str()) == -1)
        throw PosixError(format("cannot rename '%1%' to '%2%'")
            % tempLink % link);
#else
//std::cerr << "MoveFileExW '"<<tempLink<<"' -> '"<<link<<"'"<<std::endl;
    Path tempLink = (format("%1%.tmp~%2%~%3%")
        % link % GetCurrentProcessId() % rand()).str();

    SymlinkType st = createSymlink(target, tempLink);

    std::wstring wtempLink = pathW(absPath(tempLink));
    std::wstring wlink     = pathW(absPath(link)); // already absolute?
    if (!MoveFileExW(wtempLink.c_str(), wlink.c_str(), MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        // so try once more harder (atomicity suffers here)
        std::wstring old = pathW(absPath((format("%1%.old~%2%~%3%") % link % GetCurrentProcessId() % rand()).str()));
        if (!MoveFileExW(wlink.c_str(), old.c_str(), MOVEFILE_WRITE_THROUGH))
            throw WinError("MoveFileExW '%1%' -> '%2%'", to_bytes(wlink), to_bytes(old));
        // repeat
        if (!MoveFileExW(wtempLink.c_str(), wlink.c_str(), MOVEFILE_WRITE_THROUGH))
            throw WinError("MoveFileExW '%1%' -> '%2%'", tempLink, to_bytes(wlink));

        DWORD dw = GetFileAttributesW(old.c_str());
        if (dw == 0xFFFFFFFF)
            throw WinError("GetFileAttributesW '%1%'", to_bytes(old));
        if ((dw & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (!RemoveDirectoryW(old.c_str()))
                std::cerr << WinError("RemoveDirectoryW '%1%'", to_bytes(old)).msg() << std::endl;
        } else {
            if (!DeleteFileW(old.c_str()))
                std::cerr << WinError("DeleteFileW '%1%'", to_bytes(old)).msg() << std::endl;
        }
    }
#endif
}


void LocalStore::syncWithGC()
{
#ifndef _WIN32
    AutoCloseFD fdGCLock = openGCLock(ltRead);
#else
    AutoCloseWindowsHandle fdGCLock = openGCLock(ltRead);
#endif
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
        Roots roots = findRoots();
        if (roots.find(gcRoot) == roots.end())
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
#ifndef _WIN32
            AutoCloseFD fdGCLock = openGCLock(ltRead);
#else
            AutoCloseWindowsHandle fdGCLock = openGCLock(ltRead);
#endif
            if (pathExists(fnTempRoots))
                /* It *must* be stale, since there can be no two
                   processes with the same pid. */
                unlink(fnTempRoots.c_str());

            state->fdTempRoots = openLockFile(fnTempRoots, true);
#ifndef _WIN32
            fdGCLock = -1;

            debug(format("acquiring read lock on '%1%'") % fnTempRoots);
            lockFile(state->fdTempRoots.get(), ltRead, true);
            /* Check whether the garbage collector didn't get in our
               way. */
            struct stat st;
            if (fstat(state->fdTempRoots.get(), &st) == -1)
                throw PosixError(format("statting '%1%'") % fnTempRoots);
            if (st.st_size == 0) break;

            /* The garbage collector deleted this file before we could
               get a lock.  (It won't delete the file after we get a
               lock.)  Try again.

               It should not be the case on Windows because other process would fail deleting the file.
               Perhaps GCLock is not needed here too
             */
#else
            fdGCLock = INVALID_HANDLE_VALUE;

            debug(format("acquiring read lock on '%1%'") % fnTempRoots);
            assert(lockFile(state->fdTempRoots.get(), ltRead, true));
            break;
#endif
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

#ifndef _WIN32
std::set<std::pair<pid_t, Path>> LocalStore::readTempRoots(FDs & fds)
{
    std::set<std::pair<pid_t, Path>> tempRoots;

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
            throw PosixError(format("opening temporary roots file '%1%'") % path);
        }

        /* This should work, but doesn't, for some reason. */
        //FDPtr fd(new AutoCloseFD(openLockFile(path, false)));
        //if (*fd == -1) continue;

        if (path != fnTempRoots) {

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

        }

        /* Read the entire file. */
        string contents = readFile(fd->get());

        /* Extract the roots. */
        string::size_type pos = 0, end;

        while ((end = contents.find((char) 0, pos)) != string::npos) {
            Path root(contents, pos, end - pos);
            debug("got temporary root '%s'", root);
            assertStorePath(root);
            tempRoots.emplace(pid, root);
            pos = end + 1;
        }

        fds.push_back(fd); /* keep open */
    }

    return tempRoots;
}
#endif


void LocalStore::findRoots(const Path & path, unsigned char type, Roots & roots)
{
    auto foundRoot = [&](const Path & path, const Path & target) {
        Path storePath = toStorePath(target);
        if (isStorePath(storePath) && isValidPath(storePath))
            roots[path] = storePath;
        else
            printInfo(format("skipping invalid root from '%1%' to '%2%'") % path % storePath);
    };

    try {

        if (type == DT_UNKNOWN)
            type = getFileType(path);

        if (type == DT_DIR) {
            for (auto & i : readDirectory(path))
                findRoots(path + "/" + i.name(), i.type(), roots);
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
                    if (!isLink(target)) return;
                    Path target2 = readLink(target);
                    if (isInStore(target2)) foundRoot(target, target2);
                }
            }
        }

        else if (type == DT_REG) {
            Path storePath = storeDir + "/" + baseNameOf(path);
            if (isStorePath(storePath) && isValidPath(storePath))
                roots[path] = storePath;
        }

    }

    catch (PosixError & e) {
        /* We only ignore permanent failures. */
        if (e.errNo == EACCES || e.errNo == ENOENT || e.errNo == ENOTDIR)
            printInfo(format("cannot read potential root '%1%'") % path);
        else
            throw;
    } catch (WinError & e) {
        throw e; // TODO
    }
}


Roots LocalStore::findRootsNoTemp()
{
    Roots roots;

    /* Process direct roots in {gcroots,profiles}. */
    findRoots(stateDir + "/" + gcRootsDir, DT_UNKNOWN, roots);
    findRoots(stateDir + "/profiles", DT_UNKNOWN, roots);

    /* Add additional roots returned by the program specified by the
       NIX_ROOT_FINDER environment variable.  This is typically used
       to add running programs to the set of roots (to prevent them
       from being garbage collected). */
    size_t n = 0;
    for (auto & root : findRuntimeRoots())
        roots[fmt("{memory:%d}", n++)] = root;

    return roots;
}


Roots LocalStore::findRoots()
{
    Roots roots = findRootsNoTemp();
#ifndef _WIN32
    FDs fds;
    pid_t prev = -1;
    size_t n = 0;
    for (auto & root : readTempRoots(fds)) {
        if (prev != root.first) n = 0;
        prev = root.first;
        roots[fmt("{temp:%d:%d}", root.first, n++)] = root.second;
    }
#endif
    return roots;
}

#ifndef _WIN32
static void readProcLink(const string & file, StringSet & paths)
{
    /* 64 is the starting buffer size gnu readlink uses... */
    auto bufsiz = ssize_t{64};
try_again:
    char buf[bufsiz];
    auto res = readlink(file.c_str(), buf, bufsiz);
    if (res == -1) {
        if (errno == ENOENT || errno == EACCES || errno == ESRCH)
            return;
        throw PosixError("reading symlink");
    }
    if (res == bufsiz) {
        if (SSIZE_MAX / 2 < bufsiz)
            throw Error("stupidly long symlink");
        bufsiz *= 2;
        goto try_again;
    }
    if (res > 0 && buf[0] == '/')
        paths.emplace(static_cast<char *>(buf), res);
    return;
}
#endif

static string quoteRegexChars(const string & raw)
{
    static auto specialRegex = std::regex(R"([.^$\\*+?()\[\]{}|])");
    return std::regex_replace(raw, specialRegex, R"(\$&)");
}

#ifndef _WIN32
static void readFileRoots(const char * path, StringSet & paths)
{
    try {
        paths.emplace(readFile(path));
    } catch (PosixError & e) {
        if (e.errNo != ENOENT && e.errNo != EACCES)
            throw;
    }
}
#endif

PathSet LocalStore::findRuntimeRoots()
{
    PathSet roots;
#ifndef _WIN32
    StringSet paths;
    auto procDir = AutoCloseDir{opendir("/proc")};
    if (procDir) {
        struct dirent * ent;
        auto digitsRegex = std::regex(R"(^\d+$)");
        auto mapRegex = std::regex(R"(^\s*\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+(/\S+)\s*$)");
        auto storePathRegex = std::regex(quoteRegexChars(storeDir) + R"(/[0-9a-z]+[0-9a-zA-Z\+\-\._\?=]*)");
        while (errno = 0, ent = readdir(procDir.get())) {
            checkInterrupt();
            if (std::regex_match(ent->d_name, digitsRegex)) {
                readProcLink((format("/proc/%1%/exe") % ent->d_name).str(), paths);
                readProcLink((format("/proc/%1%/cwd") % ent->d_name).str(), paths);

                auto fdStr = (format("/proc/%1%/fd") % ent->d_name).str();
                auto fdDir = AutoCloseDir(opendir(fdStr.c_str()));
                if (!fdDir) {
                    if (errno == ENOENT || errno == EACCES)
                        continue;
                    throw PosixError(format("opening %1%") % fdStr);
                }
                struct dirent * fd_ent;
                while (errno = 0, fd_ent = readdir(fdDir.get())) {
                    if (fd_ent->d_name[0] != '.') {
                        readProcLink((format("%1%/%2%") % fdStr % fd_ent->d_name).str(), paths);
                    }
                }
                if (errno) {
                    if (errno == ESRCH)
                        continue;
                    throw PosixError(format("iterating /proc/%1%/fd") % ent->d_name);
                }
                fdDir.reset();

                try {
                    auto mapLines =
                        tokenizeString<std::vector<string>>(readFile((format("/proc/%1%/maps") % ent->d_name).str(), true), "\n");
                    for (const auto& line : mapLines) {
                        auto match = std::smatch{};
                        if (std::regex_match(line, match, mapRegex))
                            paths.emplace(match[1]);
                    }

                    auto envString = readFile((format("/proc/%1%/environ") % ent->d_name).str(), true);
                    auto env_end = std::sregex_iterator{};
                    for (auto i = std::sregex_iterator{envString.begin(), envString.end(), storePathRegex}; i != env_end; ++i)
                        paths.emplace(i->str());
                } catch (SysError & e) {
                    if (errno == ENOENT || errno == EACCES || errno == ESRCH)
                        continue;
                    throw;
                }
            }
        }
        if (errno)
            throw PosixError("iterating /proc");
    }

#if !defined(__linux__)
    try {
        std::regex lsofRegex(R"(^n(/.*)$)");
        auto lsofLines =
            tokenizeString<std::vector<string>>(runProgramGetStdout(LSOF, true, { "-n", "-w", "-F", "n" }), "\n");
        for (const auto & line : lsofLines) {
            std::smatch match;
            if (std::regex_match(line, match, lsofRegex))
                paths.emplace(match[1]);
        }
    } catch (ExecError & e) {
        /* lsof not installed, lsof failed */
    }
#endif

#if defined(__linux__)
    readFileRoots("/proc/sys/kernel/modprobe", paths);
    readFileRoots("/proc/sys/kernel/fbsplash", paths);
    readFileRoots("/proc/sys/kernel/poweroff_cmd", paths);
#endif

    for (auto & i : paths)
        if (isInStore(i)) {
            Path path = toStorePath(i);
            if (roots.find(path) == roots.end() && isStorePath(path) && isValidPath(path)) {
                debug(format("got additional root '%1%'") % path);
                roots.insert(path);
            }
        }
#endif
    return roots;
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
#ifndef _WIN32
    struct stat st;
    if (lstat(realPath.c_str(), &st)) {
        if (errno == ENOENT) return;
        throw PosixError(format("getting status-9 of %1%") % realPath);
    }
#else
    WIN32_FILE_ATTRIBUTE_DATA wfad;
    if (!GetFileAttributesExW(pathW(realPath).c_str(), GetFileExInfoStandard, &wfad)) {
        WinError winError("GetFileAttributesExW when deletePathRecursive '%1%'", realPath);
        if (winError.lastError == ERROR_FILE_NOT_FOUND)
            return;
        throw winError;
    }
#endif

    printInfo(format("deleting '%1%'") % path);

    state.results.paths.insert(path);

    /* If the path is not a regular file or symlink, move it to the
       trash directory.  The move is to ensure that later (when we're
       not holding the global GC lock) we can delete the path without
       being afraid that the path has become alive again.  Otherwise
       delete it right away. */
#ifndef _WIN32
    if (state.moveToTrash && S_ISDIR(st.st_mode)) {
        // Estimate the amount freed using the narSize field.  FIXME:
        // if the path was not valid, need to determine the actual
        // size.
        try {
            if (chmod(realPath.c_str(), st.st_mode | S_IWUSR) == -1)
                throw PosixError(format("making '%1%' writable") % realPath);
            Path tmp = trashDir + "/" + baseNameOf(path);
            if (rename(realPath.c_str(), tmp.c_str()))
                throw PosixError(format("unable to rename '%1%' to '%2%'") % realPath % tmp);
            state.bytesInvalidated += size;
        } catch (PosixError & e) {
            if (e.errNo == ENOSPC) {
                printInfo(format("note: can't create move '%1%': %2%") % realPath % e.msg());
                deleteGarbage(state, realPath);
            }
        }
#else
    if (state.moveToTrash && (wfad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        try {
            Path tmp = trashDir + "/" + baseNameOf(path);
            if (!MoveFileExW(pathW(realPath).c_str(), pathW(tmp).c_str(), MOVEFILE_WRITE_THROUGH))
                throw WinError(format("unable to rename '%1%' to '%2%'") % realPath % tmp);
            state.bytesInvalidated += size;
        } catch (WinError & e) {
            throw e; // TODO
        }
#endif
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
#ifndef _WIN32
    AutoCloseDir dir(opendir(linksDir.c_str()));
    if (!dir) throw PosixError(format("opening directory '%1%'") % linksDir);

    long long actualSize = 0, unsharedSize = 0;

    struct dirent * dirent;
    while (errno = 0, dirent = readdir(dir.get())) {
        checkInterrupt();
        string name = dirent->d_name;
        if (name == "." || name == "..") continue;
        Path path = linksDir + "/" + name;

        struct stat st;
        if (lstat(path.c_str(), &st) == -1)
            throw PosixError(format("statting '%1%'") % path);

        if (st.st_nlink != 1) {
            unsigned long long size = st.st_blocks * 512ULL;
            actualSize += size;
            unsharedSize += (st.st_nlink - 1) * size;
            continue;
        }

        printMsg(lvlTalkative, format("deleting unused link '%1%'") % path);

        if (unlink(path.c_str()) == -1)
            throw PosixError(format("deleting '%1%'") % path);
        state.results.bytesFreed += st.st_blocks * 512ULL;
    }

    struct stat st;
    if (stat(linksDir.c_str(), &st) == -1)
        throw PosixError(format("statting '%1%'") % linksDir);
    long long overhead = st.st_blocks * 512ULL;

    printInfo(format("note: currently hard linking saves %.2f MiB")
        % ((unsharedSize - actualSize - overhead) / (1024.0 * 1024.0)));
#else
    long long actualSize = 0, unsharedSize = 0;

    WIN32_FIND_DATAW wfd;
    std::wstring wlinksDir = pathW(linksDir);
    HANDLE hFind = FindFirstFileExW((wlinksDir + L"\\*").c_str(), FindExInfoBasic, &wfd, FindExSearchNameMatch, NULL, 0);
    if (hFind == INVALID_HANDLE_VALUE) {
        throw WinError("FindFirstFileExW when LocalStore::removeUnusedLinks()");
    } else {
        do {
            bool isDot    = (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && wfd.cFileName[0]==L'.' && wfd.cFileName[1]==L'\0';
            bool isDotDot = (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && wfd.cFileName[0]==L'.' && wfd.cFileName[1]==L'.' && wfd.cFileName[2]==L'\0';
            if (isDot || isDotDot)
                continue;

            checkInterrupt();

            BY_HANDLE_FILE_INFORMATION bhfi;
            std::wstring wpath = wlinksDir + L'\\' + wfd.cFileName;
            HANDLE hFile = CreateFileW(wpath.c_str(), 0, FILE_SHARE_READ, 0, OPEN_EXISTING,
                                       FILE_FLAG_POSIX_SEMANTICS |
                                       ((wfd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ? FILE_FLAG_OPEN_REPARSE_POINT : 0) |
                                       ((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY    ) != 0 ? FILE_FLAG_BACKUP_SEMANTICS   : 0),
                                       0);
            if (hFile == INVALID_HANDLE_VALUE)
                throw WinError("CreateFileW when LocalStore::removeUnusedLinks() '%1%'", to_bytes(wpath));
            if (!GetFileInformationByHandle(hFile, &bhfi))
                throw WinError("GetFileInformationByHandle when LocalStore::removeUnusedLinks() '%1%'", to_bytes(wpath));
            CloseHandle(hFile);

            uint64_t size = (uint64_t(bhfi.nFileSizeHigh) << 32) + bhfi.nFileSizeLow;
            if (bhfi.nNumberOfLinks != 1) {
                actualSize += size;
                unsharedSize += (bhfi.nNumberOfLinks - 1) * size;
                continue;
            }

            printMsg(lvlTalkative, format("deleting unused link '%1%'") % to_bytes(wpath));
    
            if (!DeleteFileW(wpath.c_str()))
                throw WinError("DeleteFileW when LocalStore::removeUnusedLinks() '%1%'", to_bytes(wpath));
            state.results.bytesFreed += size;

        } while(FindNextFileW(hFind, &wfd));
        WinError winError("FindNextFileW when LocalStore::removeUnusedLinks()");
        if (winError.lastError != ERROR_NO_MORE_FILES)
            throw winError;
        FindClose(hFind);
    }

    printInfo(format("note: currently hard linking saves %.2f MiB")
        % ((unsharedSize - actualSize) / (1024.0 * 1024.0)));
#endif
}


void LocalStore::collectGarbage(const GCOptions & options, GCResults & results)
{
std::cerr << "LocalStore::collectGarbage" <<std::endl;
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
#ifndef _WIN32
    AutoCloseFD fdGCLock = openGCLock(ltWrite);
#else
    AutoCloseWindowsHandle fdGCLock = openGCLock(ltWrite);
#endif

    /* Find the roots.  Since we've grabbed the GC lock, the set of
       permanent roots cannot increase now. */
    printError(format("finding garbage collector roots..."));
    Roots rootMap = options.ignoreLiveness ? Roots() : findRootsNoTemp();

    for (auto & i : rootMap) state.roots.insert(i.second);
#ifndef _WIN32
    /* Read the temporary roots.  This acquires read locks on all
       per-process temporary root files.  So after this point no paths
       can be added to the set of temporary roots. */
    FDs fds;
    for (auto & root : readTempRoots(fds))
        state.tempRoots.insert(root.second);
#endif
    state.roots.insert(state.tempRoots.begin(), state.tempRoots.end());

    /* After this point the set of roots or temporary roots cannot
       increase, since we hold locks on everything.  So everything
       that is not reachable from `roots' is garbage. */

    if (state.shouldDelete) {
        if (pathExists(trashDir)) deleteGarbage(state, trashDir);
        try {
            createDirs(trashDir);
        } catch (PosixError & e) {
            if (e.errNo == ENOSPC) {
                printInfo(format("note: can't create trash directory: %1%") % e.msg());
                state.moveToTrash = false;
            }
        } catch (WinError & e) {
            throw e; // TODO
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
            Paths entries;
#ifndef _WIN32
            AutoCloseDir dir(opendir(realStoreDir.c_str()));
            if (!dir) throw PosixError(format("opening directory '%1%'") % realStoreDir);

            /* Read the store and immediately delete all paths that
               aren't valid.  When using --max-freed etc., deleting
               invalid paths is preferred over deleting unreachable
               paths, since unreachable paths could become reachable
               again.  We don't use readDirectory() here so that GCing
               can start faster. */
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
#else
            WIN32_FIND_DATAW wfd;
            HANDLE hFind = FindFirstFileExW((pathW(realStoreDir) + L"\\*").c_str(), FindExInfoBasic, &wfd, FindExSearchNameMatch, NULL, 0);
            if (hFind == INVALID_HANDLE_VALUE) {
                throw WinError("FindFirstFileExW when collectGarbage '%1%'", realStoreDir);
            } else {
                do {
                    checkInterrupt();
                    if ((wfd.cFileName[0] == '.' && wfd.cFileName[1] == '\0')
                     || (wfd.cFileName[0] == '.' && wfd.cFileName[1] == '.' && wfd.cFileName[2] == '\0')) {
                    } else {
                        Path path = storeDir + "/" + to_bytes(wfd.cFileName);
                        if (isStorePath(path) && isValidPath(path))
                            entries.push_back(path);
                        else
                            tryToDelete(state, path);
                    }
                } while(FindNextFileW(hFind, &wfd));
                WinError winError("FindNextFileW when collectGarbage '%1%'", realStoreDir);
                if (winError.lastError != ERROR_NO_MORE_FILES)
                    throw winError;
                FindClose(hFind);
            }
#endif

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

#ifndef _NDEBUG
    /* paths shouldn't be  dead and alive at the same time */
    PathSet deadAndAlive;
    std::set_intersection(state.dead.begin(), state.dead.end(),
                          state.alive.begin(), state.alive.end(),
                          std::inserter(deadAndAlive, deadAndAlive.begin()));
    assert(deadAndAlive.size() == 0);
#endif

    if (state.options.action == GCOptions::gcReturnLive) {
        state.results.paths = state.alive;
        return;
    }

    if (state.options.action == GCOptions::gcReturnDead) {
        state.results.paths = state.dead;
        return;
    }

#ifndef _WIN32
    /* Allow other processes to add to the store from here on. */
    fdGCLock = -1;
    fds.clear();
#else
    fdGCLock = INVALID_HANDLE_VALUE;
#endif

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
#ifdef HAVE_STATVFS
    auto getAvail = [this]() {
        struct statvfs st;
        if (statvfs(realStoreDir.c_str(), &st))
            throw PosixError("getting filesystem info about '%s'", realStoreDir);

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

        if (now < state->lastGCCheck + std::chrono::seconds(5)) return;

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

                printInfo("running auto-GC to free %d bytes", settings.maxFree - avail);

                GCOptions options;
                options.maxFreed = settings.maxFree - avail;

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
#endif
}


}
