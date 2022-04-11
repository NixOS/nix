#include "derivations.hh"
#include "globals.hh"
#include "local-store.hh"
#include "local-fs-store.hh"
#include "finally.hh"

#include <functional>
#include <queue>
#include <algorithm>
#include <regex>
#include <random>

#include <climits>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

namespace nix {


static std::string gcSocketPath = "/gc-socket/socket";
static std::string gcRootsDir = "gcroots";


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
        throw SysError("cannot rename '%1%' to '%2%'",
            tempLink , link);
}


void LocalStore::addIndirectRoot(const Path & path)
{
    std::string hash = hashString(htSHA1, path).to_string(Base32, false);
    Path realRoot = canonPath(fmt("%1%/%2%/auto/%3%", stateDir, gcRootsDir, hash));
    makeSymlink(realRoot, path);
}


Path LocalFSStore::addPermRoot(const StorePath & storePath, const Path & _gcRoot)
{
    Path gcRoot(canonPath(_gcRoot));

    if (isInStore(gcRoot))
        throw Error(
                "creating a garbage collector root (%1%) in the Nix store is forbidden "
                "(are you running nix-build inside the store?)", gcRoot);

    /* Register this root with the garbage collector, if it's
       running. This should be superfluous since the caller should
       have registered this root yet, but let's be on the safe
       side. */
    addTempRoot(storePath);

    /* Don't clobber the link if it already exists and doesn't
       point to the Nix store. */
    if (pathExists(gcRoot) && (!isLink(gcRoot) || !isInStore(readLink(gcRoot))))
        throw Error("cannot create symlink '%1%'; already exists", gcRoot);
    makeSymlink(gcRoot, printStorePath(storePath));
    addIndirectRoot(gcRoot);

    return gcRoot;
}


void LocalStore::addTempRoot(const StorePath & path)
{
    auto state(_state.lock());

    /* Create the temporary roots file for this process. */
    if (!state->fdTempRoots) {

        while (1) {
            if (pathExists(fnTempRoots))
                /* It *must* be stale, since there can be no two
                   processes with the same pid. */
                unlink(fnTempRoots.c_str());

            state->fdTempRoots = openLockFile(fnTempRoots, true);

            debug("acquiring write lock on '%s'", fnTempRoots);
            lockFile(state->fdTempRoots.get(), ltWrite, true);

            /* Check whether the garbage collector didn't get in our
               way. */
            struct stat st;
            if (fstat(state->fdTempRoots.get(), &st) == -1)
                throw SysError("statting '%1%'", fnTempRoots);
            if (st.st_size == 0) break;

            /* The garbage collector deleted this file before we could
               get a lock.  (It won't delete the file after we get a
               lock.)  Try again. */
        }

    }

    if (!state->fdGCLock)
        state->fdGCLock = openGCLock();

 restart:
    FdLock gcLock(state->fdGCLock.get(), ltRead, false, "");

    if (!gcLock.acquired) {
        /* We couldn't get a shared global GC lock, so the garbage
           collector is running. So we have to connect to the garbage
           collector and inform it about our root. */
        if (!state->fdRootsSocket) {
            auto socketPath = stateDir.get() + gcSocketPath;
            debug("connecting to '%s'", socketPath);
            state->fdRootsSocket = createUnixDomainSocket();
            try {
                nix::connect(state->fdRootsSocket.get(), socketPath);
            } catch (SysError & e) {
                /* The garbage collector may have exited, so we need to
                   restart. */
                if (e.errNo == ECONNREFUSED) {
                    debug("GC socket connection refused");
                    state->fdRootsSocket.close();
                    goto restart;
                }
            }
        }

        try {
            debug("sending GC root '%s'", printStorePath(path));
            writeFull(state->fdRootsSocket.get(), printStorePath(path) + "\n", false);
            char c;
            readFull(state->fdRootsSocket.get(), &c, 1);
            assert(c == '1');
            debug("got ack for GC root '%s'", printStorePath(path));
        } catch (SysError & e) {
            /* The garbage collector may have exited, so we need to
               restart. */
            if (e.errNo == EPIPE) {
                debug("GC socket disconnected");
                state->fdRootsSocket.close();
                goto restart;
            }
        } catch (EndOfFile & e) {
            debug("GC socket disconnected");
            state->fdRootsSocket.close();
            goto restart;
        }
    }

    /* Append the store path to the temporary roots file. */
    auto s = printStorePath(path) + '\0';
    writeFull(state->fdTempRoots.get(), s);
}


static std::string censored = "{censored}";


void LocalStore::findTempRoots(Roots & tempRoots, bool censor)
{
    /* Read the `temproots' directory for per-process temporary root
       files. */
    for (auto & i : readDirectory(tempRootsDir)) {
        if (i.name[0] == '.') {
            // Ignore hidden files. Some package managers (notably portage) create
            // those to keep the directory alive.
            continue;
        }
        Path path = tempRootsDir + "/" + i.name;

        pid_t pid = std::stoi(i.name);

        debug(format("reading temporary root file '%1%'") % path);
        AutoCloseFD fd(open(path.c_str(), O_CLOEXEC | O_RDWR, 0666));
        if (!fd) {
            /* It's okay if the file has disappeared. */
            if (errno == ENOENT) continue;
            throw SysError("opening temporary roots file '%1%'", path);
        }

        /* Try to acquire a write lock without blocking.  This can
           only succeed if the owning process has died.  In that case
           we don't care about its temporary roots. */
        if (lockFile(fd.get(), ltWrite, false)) {
            printInfo("removing stale temporary roots file '%1%'", path);
            unlink(path.c_str());
            writeFull(fd.get(), "d");
            continue;
        }

        /* Read the entire file. */
        auto contents = readFile(fd.get());

        /* Extract the roots. */
        std::string::size_type pos = 0, end;

        while ((end = contents.find((char) 0, pos)) != std::string::npos) {
            Path root(contents, pos, end - pos);
            debug("got temporary root '%s'", root);
            tempRoots[parseStorePath(root)].emplace(censor ? censored : fmt("{temp:%d}", pid));
            pos = end + 1;
        }
    }
}


void LocalStore::findRoots(const Path & path, unsigned char type, Roots & roots)
{
    auto foundRoot = [&](const Path & path, const Path & target) {
        try {
            auto storePath = toStorePath(target).first;
            if (isValidPath(storePath))
                roots[std::move(storePath)].emplace(path);
            else
                printInfo("skipping invalid root from '%1%' to '%2%'", path, target);
        } catch (BadStorePath &) { }
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
            auto storePath = maybeParseStorePath(storeDir + "/" + std::string(baseNameOf(path)));
            if (storePath && isValidPath(*storePath))
                roots[std::move(*storePath)].emplace(path);
        }

    }

    catch (SysError & e) {
        /* We only ignore permanent failures. */
        if (e.errNo == EACCES || e.errNo == ENOENT || e.errNo == ENOTDIR)
            printInfo("cannot read potential root '%1%'", path);
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

    Pipe fromHelper;
    fromHelper.create();
    Pid helperPid = startProcess([&]() {
        if (dup2(fromHelper.writeSide.get(), STDOUT_FILENO) == -1)
            throw SysError("cannot pipe standard output into log file");
        if (chdir("/") == -1) throw SysError("changing into /");
        auto helperProgram = settings.nixLibexecDir + "/nix/nix-find-roots";
        Strings args = {std::string(baseNameOf(helperProgram))};
        execv(
            helperProgram.c_str(),
            stringsToCharPtrs(args).data()
        );

        throw SysError("executing '%s'", helperProgram);
    });

    try {
        while (true) {
            auto line = readLine(fromHelper.readSide.get());
            if (line.empty()) break; // TODO: Handle the broken symlinks
            auto parsedLine = tokenizeString<std::vector<std::string>>(line, "\t");
            if (parsedLine.size() != 2)
                throw Error("Invalid result from the gc helper");
            auto rawDestPath = parsedLine[0];
            if (!isInStore(rawDestPath)) continue;
            auto destPath = toStorePath(rawDestPath).first;
            if (!isValidPath(destPath)) continue;
            roots[destPath].insert(parsedLine[1]);
        }
    } catch (EndOfFile &) {
    }

    int res = helperPid.wait();
    if (res != 0)
        throw Error("unable to start the gc helper process");

    return roots;
}

typedef std::unordered_map<Path, std::unordered_set<std::string>> UncheckedRoots;

static void readProcLink(const std::string & file, UncheckedRoots & roots)
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

static std::string quoteRegexChars(const std::string & raw)
{
    static auto specialRegex = std::regex(R"([.^$\\*+?()\[\]{}|])");
    return std::regex_replace(raw, specialRegex, R"(\$&)");
}

#if __linux__
static void readFileRoots(const char * path, UncheckedRoots & roots)
{
    try {
        roots[readFile(path)].emplace(path);
    } catch (SysError & e) {
        if (e.errNo != ENOENT && e.errNo != EACCES)
            throw;
    }
}
#endif

void LocalStore::findRuntimeRoots(Roots & roots, bool censor)
{
    UncheckedRoots unchecked;

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
                    throw SysError("opening %1%", fdStr);
                }
                struct dirent * fd_ent;
                while (errno = 0, fd_ent = readdir(fdDir.get())) {
                    if (fd_ent->d_name[0] != '.')
                        readProcLink(fmt("%s/%s", fdStr, fd_ent->d_name), unchecked);
                }
                if (errno) {
                    if (errno == ESRCH)
                        continue;
                    throw SysError("iterating /proc/%1%/fd", ent->d_name);
                }
                fdDir.reset();

                try {
                    auto mapFile = fmt("/proc/%s/maps", ent->d_name);
                    auto mapLines = tokenizeString<std::vector<std::string>>(readFile(mapFile), "\n");
                    for (const auto & line : mapLines) {
                        auto match = std::smatch{};
                        if (std::regex_match(line, match, mapRegex))
                            unchecked[match[1]].emplace(mapFile);
                    }

                    auto envFile = fmt("/proc/%s/environ", ent->d_name);
                    auto envString = readFile(envFile);
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
    if (getEnv("_NIX_TEST_NO_LSOF") != "1") {
        try {
            std::regex lsofRegex(R"(^n(/.*)$)");
            auto lsofLines =
                tokenizeString<std::vector<std::string>>(runProgram(LSOF, true, { "-n", "-w", "-F", "n" }), "\n");
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

#if __linux__
    readFileRoots("/proc/sys/kernel/modprobe", unchecked);
    readFileRoots("/proc/sys/kernel/fbsplash", unchecked);
    readFileRoots("/proc/sys/kernel/poweroff_cmd", unchecked);
#endif

    for (auto & [target, links] : unchecked) {
        if (!isInStore(target)) continue;
        try {
            auto path = toStorePath(target).first;
            if (!isValidPath(path)) continue;
            debug("got additional root '%1%'", printStorePath(path));
            if (censor)
                roots[path].insert(censored);
            else
                roots[path].insert(links.begin(), links.end());
        } catch (BadStorePath &) { }
    }
}


struct GCLimitReached { };


void LocalStore::collectGarbage(const GCOptions & options, GCResults & results)
{
    bool shouldDelete = options.action == GCOptions::gcDeleteDead || options.action == GCOptions::gcDeleteSpecific;
    bool gcKeepOutputs = settings.gcKeepOutputs;
    bool gcKeepDerivations = settings.gcKeepDerivations;

    StorePathSet roots, dead, alive;

    struct Shared
    {
        // The temp roots only store the hash part to make it easier to
        // ignore suffixes like '.lock', '.chroot' and '.check'.
        std::unordered_set<std::string> tempRoots;

        // Hash part of the store path currently being deleted, if
        // any.
        std::optional<std::string> pending;
    };

    Sync<Shared> _shared;

    std::condition_variable wakeup;

    /* Using `--ignore-liveness' with `--delete' can have unintended
       consequences if `keep-outputs' or `keep-derivations' are true
       (the garbage collector will recurse into deleting the outputs
       or derivers, respectively).  So disable them. */
    if (options.action == GCOptions::gcDeleteSpecific && options.ignoreLiveness) {
        gcKeepOutputs = false;
        gcKeepDerivations = false;
    }

    if (shouldDelete)
        deletePath(reservedPath);

    /* Acquire the global GC root. Note: we don't use fdGCLock
       here because then in auto-gc mode, another thread could
       downgrade our exclusive lock. */
    auto fdGCLock = openGCLock();
    FdLock gcLock(fdGCLock.get(), ltWrite, true, "waiting for the big garbage collector lock...");

    /* Start the server for receiving new roots. */
    auto socketPath = stateDir.get() + gcSocketPath;
    createDirs(dirOf(socketPath));
    auto fdServer = createUnixDomainSocket(socketPath, 0666);

    if (fcntl(fdServer.get(), F_SETFL, fcntl(fdServer.get(), F_GETFL) | O_NONBLOCK) == -1)
        throw SysError("making socket '%1%' non-blocking", socketPath);

    Pipe shutdownPipe;
    shutdownPipe.create();

    std::thread serverThread([&]() {
        Sync<std::map<int, std::thread>> connections;

        Finally cleanup([&]() {
            debug("GC roots server shutting down");
            while (true) {
                auto item = remove_begin(*connections.lock());
                if (!item) break;
                auto & [fd, thread] = *item;
                shutdown(fd, SHUT_RDWR);
                thread.join();
            }
        });

        while (true) {
            std::vector<struct pollfd> fds;
            fds.push_back({.fd = shutdownPipe.readSide.get(), .events = POLLIN});
            fds.push_back({.fd = fdServer.get(), .events = POLLIN});
            auto count = poll(fds.data(), fds.size(), -1);
            assert(count != -1);

            if (fds[0].revents)
                /* Parent is asking us to quit. */
                break;

            if (fds[1].revents) {
                /* Accept a new connection. */
                assert(fds[1].revents & POLLIN);
                AutoCloseFD fdClient = accept(fdServer.get(), nullptr, nullptr);
                if (!fdClient) continue;

                debug("GC roots server accepted new client");

                /* Process the connection in a separate thread. */
                auto fdClient_ = fdClient.get();
                std::thread clientThread([&, fdClient = std::move(fdClient)]() {
                    Finally cleanup([&]() {
                        auto conn(connections.lock());
                        auto i = conn->find(fdClient.get());
                        if (i != conn->end()) {
                            i->second.detach();
                            conn->erase(i);
                        }
                    });

                    /* On macOS, accepted sockets inherit the
                       non-blocking flag from the server socket, so
                       explicitly make it blocking. */
                    if (fcntl(fdServer.get(), F_SETFL, fcntl(fdServer.get(), F_GETFL) & ~O_NONBLOCK) == -1)
                        abort();

                    while (true) {
                        try {
                            auto path = readLine(fdClient.get());
                            auto storePath = maybeParseStorePath(path);
                            if (storePath) {
                                debug("got new GC root '%s'", path);
                                auto hashPart = std::string(storePath->hashPart());
                                auto shared(_shared.lock());
                                shared->tempRoots.insert(hashPart);
                                /* If this path is currently being
                                   deleted, then we have to wait until
                                   deletion is finished to ensure that
                                   the client doesn't start
                                   re-creating it before we're
                                   done. FIXME: ideally we would use a
                                   FD for this so we don't block the
                                   poll loop. */
                                while (shared->pending == hashPart) {
                                    debug("synchronising with deletion of path '%s'", path);
                                    shared.wait(wakeup);
                                }
                            } else
                                printError("received garbage instead of a root from client");
                            writeFull(fdClient.get(), "1", false);
                        } catch (Error & e) {
                            debug("reading GC root from client: %s", e.msg());
                            break;
                        }
                    }
                });

                connections.lock()->insert({fdClient_, std::move(clientThread)});
            }
        }
    });

    Finally stopServer([&]() {
        writeFull(shutdownPipe.writeSide.get(), "x", false);
        wakeup.notify_all();
        if (serverThread.joinable()) serverThread.join();
    });

    /* Find the roots.  Since we've grabbed the GC lock, the set of
       permanent roots cannot increase now. */
    printInfo("finding garbage collector roots...");
    Roots rootMap;
    if (!options.ignoreLiveness)
        findRootsNoTemp(rootMap, true);

    for (auto & i : rootMap) roots.insert(i.first);

    /* Read the temporary roots created before we acquired the global
       GC root. Any new roots will be sent to our socket. */
    Roots tempRoots;
    findTempRoots(tempRoots, true);
    for (auto & root : tempRoots) {
        _shared.lock()->tempRoots.insert(std::string(root.first.hashPart()));
        roots.insert(root.first);
    }

    /* Helper function that deletes a path from the store and throws
       GCLimitReached if we've deleted enough garbage. */
    auto deleteFromStore = [&](std::string_view baseName)
    {
        Path path = storeDir + "/" + std::string(baseName);
        Path realPath = realStoreDir + "/" + std::string(baseName);

        printInfo("deleting '%1%'", path);

        results.paths.insert(path);

        uint64_t bytesFreed;
        deletePath(realPath, bytesFreed);
        results.bytesFreed += bytesFreed;

        if (results.bytesFreed > options.maxFreed) {
            printInfo("deleted more than %d bytes; stopping", options.maxFreed);
            throw GCLimitReached();
        }
    };

    std::map<StorePath, StorePathSet> referrersCache;

    /* Helper function that visits all paths reachable from `start`
       via the referrers edges and optionally derivers and derivation
       output edges. If none of those paths are roots, then all
       visited paths are garbage and are deleted. */
    auto deleteReferrersClosure = [&](const StorePath & start) {
        StorePathSet visited;
        std::queue<StorePath> todo;

        /* Wake up any GC client waiting for deletion of the paths in
           'visited' to finish. */
        Finally releasePending([&]() {
            auto shared(_shared.lock());
            shared->pending.reset();
            wakeup.notify_all();
        });

        auto enqueue = [&](const StorePath & path) {
            if (visited.insert(path).second)
                todo.push(path);
        };

        enqueue(start);

        while (auto path = pop(todo)) {
            checkInterrupt();

            /* Bail out if we've previously discovered that this path
               is alive. */
            if (alive.count(*path)) {
                alive.insert(start);
                return;
            }

            /* If we've previously deleted this path, we don't have to
               handle it again. */
            if (dead.count(*path)) continue;

            auto markAlive = [&]()
            {
                alive.insert(*path);
                alive.insert(start);
                try {
                    StorePathSet closure;
                    computeFSClosure(*path, closure);
                    for (auto & p : closure)
                        alive.insert(p);
                } catch (InvalidPath &) { }
            };

            /* If this is a root, bail out. */
            if (roots.count(*path)) {
                debug("cannot delete '%s' because it's a root", printStorePath(*path));
                return markAlive();
            }

            if (options.action == GCOptions::gcDeleteSpecific
                && !options.pathsToDelete.count(*path))
                return;

            {
                auto hashPart = std::string(path->hashPart());
                auto shared(_shared.lock());
                if (shared->tempRoots.count(hashPart)) {
                    debug("cannot delete '%s' because it's a temporary root", printStorePath(*path));
                    return markAlive();
                }
                shared->pending = hashPart;
            }

            if (isValidPath(*path)) {

                /* Visit the referrers of this path. */
                auto i = referrersCache.find(*path);
                if (i == referrersCache.end()) {
                    StorePathSet referrers;
                    queryReferrers(*path, referrers);
                    referrersCache.emplace(*path, std::move(referrers));
                    i = referrersCache.find(*path);
                }
                for (auto & p : i->second)
                    enqueue(p);

                /* If keep-derivations is set and this is a
                   derivation, then visit the derivation outputs. */
                if (gcKeepDerivations && path->isDerivation()) {
                    for (auto & [name, maybeOutPath] : queryPartialDerivationOutputMap(*path))
                        if (maybeOutPath &&
                            isValidPath(*maybeOutPath) &&
                            queryPathInfo(*maybeOutPath)->deriver == *path)
                            enqueue(*maybeOutPath);
                }

                /* If keep-outputs is set, then visit the derivers. */
                if (gcKeepOutputs) {
                    auto derivers = queryValidDerivers(*path);
                    for (auto & i : derivers)
                        enqueue(i);
                }
            }
        }

        for (auto & path : topoSortPaths(visited)) {
            if (!dead.insert(path).second) continue;
            if (shouldDelete) {
                invalidatePathChecked(path);
                deleteFromStore(path.to_string());
                referrersCache.erase(path);
            }
        }
    };

    /* Synchronisation point for testing, see tests/gc-concurrent.sh. */
    if (auto p = getEnv("_NIX_TEST_GC_SYNC"))
        readFile(*p);

    /* Either delete all garbage paths, or just the specified
       paths (for gcDeleteSpecific). */
    if (options.action == GCOptions::gcDeleteSpecific) {

        for (auto & i : options.pathsToDelete) {
            deleteReferrersClosure(i);
            if (!dead.count(i))
                throw Error(
                    "Cannot delete path '%1%' since it is still alive. "
                    "To find out why, use: "
                    "nix-store --query --roots",
                    printStorePath(i));
        }

    } else if (options.maxFreed > 0) {

        if (shouldDelete)
            printInfo("deleting garbage...");
        else
            printInfo("determining live/dead paths...");

        try {
            AutoCloseDir dir(opendir(realStoreDir.get().c_str()));
            if (!dir) throw SysError("opening directory '%1%'", realStoreDir);

            /* Read the store and delete all paths that are invalid or
               unreachable. We don't use readDirectory() here so that
               GCing can start faster. */
            auto linksName = baseNameOf(linksDir);
            Paths entries;
            struct dirent * dirent;
            while (errno = 0, dirent = readdir(dir.get())) {
                checkInterrupt();
                std::string name = dirent->d_name;
                if (name == "." || name == ".." || name == linksName) continue;

                if (auto storePath = maybeParseStorePath(storeDir + "/" + name))
                    deleteReferrersClosure(*storePath);
                else
                    deleteFromStore(name);

            }
        } catch (GCLimitReached & e) {
        }
    }

    if (options.action == GCOptions::gcReturnLive) {
        for (auto & i : alive)
            results.paths.insert(printStorePath(i));
        return;
    }

    if (options.action == GCOptions::gcReturnDead) {
        for (auto & i : dead)
            results.paths.insert(printStorePath(i));
        return;
    }

    /* Unlink all files in /nix/store/.links that have a link count of 1,
       which indicates that there are no other links and so they can be
       safely deleted.  FIXME: race condition with optimisePath(): we
       might see a link count of 1 just before optimisePath() increases
       the link count. */
    if (options.action == GCOptions::gcDeleteDead || options.action == GCOptions::gcDeleteSpecific) {
        printInfo("deleting unused links...");

        AutoCloseDir dir(opendir(linksDir.c_str()));
        if (!dir) throw SysError("opening directory '%1%'", linksDir);

        int64_t actualSize = 0, unsharedSize = 0;

        struct dirent * dirent;
        while (errno = 0, dirent = readdir(dir.get())) {
            checkInterrupt();
            std::string name = dirent->d_name;
            if (name == "." || name == "..") continue;
            Path path = linksDir + "/" + name;

            auto st = lstat(path);

            if (st.st_nlink != 1) {
                actualSize += st.st_size;
                unsharedSize += (st.st_nlink - 1) * st.st_size;
                continue;
            }

            printMsg(lvlTalkative, format("deleting unused link '%1%'") % path);

            if (unlink(path.c_str()) == -1)
                throw SysError("deleting '%1%'", path);

            results.bytesFreed += st.st_size;
        }

        struct stat st;
        if (stat(linksDir.c_str(), &st) == -1)
            throw SysError("statting '%1%'", linksDir);
        int64_t overhead = st.st_blocks * 512ULL;

        printInfo("note: currently hard linking saves %.2f MiB",
            ((unsharedSize - actualSize - overhead) / (1024.0 * 1024.0)));
    }

    /* While we're at it, vacuum the database. */
    //if (options.action == GCOptions::gcDeleteDead) vacuumDB();
}


void LocalStore::autoGC(bool sync)
{
    static auto fakeFreeSpaceFile = getEnv("_NIX_TEST_FREE_SPACE_FILE");

    auto getAvail = [this]() -> uint64_t {
        if (fakeFreeSpaceFile)
            return std::stoll(readFile(*fakeFreeSpaceFile));

        struct statvfs st;
        if (statvfs(realStoreDir.get().c_str(), &st))
            throw SysError("getting filesystem info about '%s'", realStoreDir);

        return (uint64_t) st.f_bavail * st.f_frsize;
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
