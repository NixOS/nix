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
    string hash = hashString(htSHA1, path).to_string(Base32, false);
    Path realRoot = canonPath((format("%1%/%2%/auto/%3%")
        % stateDir % gcRootsDir % hash).str());
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

 restart:
    FdLock gcLock(state->fdGCLock.get(), ltRead, false, "");

    if (!gcLock.acquired) {
        /* We couldn't get a shared global GC lock, so the garbage
           collector is running. So we have to connect to the garbage
           collector and inform it about our root. */
        if (!state->fdRootsSocket) {
            state->fdRootsSocket = createUnixDomainSocket();

            auto socketPath = stateDir.get() + gcSocketPath;

            debug("connecting to '%s'", socketPath);

            struct sockaddr_un addr;
            addr.sun_family = AF_UNIX;
            if (socketPath.size() + 1 >= sizeof(addr.sun_path))
                throw Error("socket path '%s' is too long", socketPath);
            strcpy(addr.sun_path, socketPath.c_str());

            if (::connect(state->fdRootsSocket.get(), (struct sockaddr *) &addr, sizeof(addr)) == -1)
                throw SysError("cannot connect to garbage collector at '%s'", socketPath);
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
    string s = printStorePath(path) + '\0';
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
        string contents = readFile(fd.get());

        /* Extract the roots. */
        string::size_type pos = 0, end;

        while ((end = contents.find((char) 0, pos)) != string::npos) {
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
    findRootsNoTemp(roots, censor);

    findTempRoots(roots, censor);

    return roots;
}

typedef std::unordered_map<Path, std::unordered_set<std::string>> UncheckedRoots;

static void readProcLink(const string & file, UncheckedRoots & roots)
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

static void readFileRoots(const char * path, UncheckedRoots & roots)
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
                    auto mapLines = tokenizeString<std::vector<string>>(readFile(mapFile), "\n");
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


struct LocalStore::GCState
{
    const GCOptions & options;
    GCResults & results;
    StorePathSet roots;
    StorePathSet dead;
    StorePathSet alive;
    bool gcKeepOutputs;
    bool gcKeepDerivations;
    bool shouldDelete;

    struct Shared
    {
        // The temp roots only store the hash part to make it easier to
        // ignore suffixes like '.lock', '.chroot' and '.check'.
        std::unordered_set<std::string> tempRoots;

        // Hash part of the store path currently being deleted, if
        // any.
        std::optional<std::string> pending;
    };

    Sync<Shared> shared;

    std::condition_variable wakeup;

    GCState(const GCOptions & options, GCResults & results)
        : options(options), results(results) { }
};


bool LocalStore::tryToDelete(
    GCState & state,
    StorePathSet & visited,
    const Path & path,
    bool recursive)
{
    checkInterrupt();

    auto realPath = realStoreDir + "/" + std::string(baseNameOf(path));
    if (realPath == linksDir) return false;

    //Activity act(*logger, lvlDebug, format("considering whether to delete '%1%'") % path);

    auto storePath = maybeParseStorePath(path);

    /* Wake up any client waiting for deletion of this path to
       finish. */
    Finally releasePending([&]() {
        auto shared(state.shared.lock());
        shared->pending.reset();
        state.wakeup.notify_all();
    });

    if (storePath) {

        if (!visited.insert(*storePath).second) return true;

        if (state.alive.count(*storePath)) return false;

        if (state.dead.count(*storePath)) return true;

        if (state.roots.count(*storePath)) {
            debug("cannot delete '%s' because it's a root", path);
            state.alive.insert(*storePath);
            return false;
        }

        if (isValidPath(*storePath)) {
            StorePathSet incoming;

            /* Don't delete this path if any of its referrers are alive. */
            queryReferrers(*storePath, incoming);

            /* If keep-derivations is set and this is a derivation, then
               don't delete the derivation if any of the outputs are alive. */
            if (state.gcKeepDerivations && storePath->isDerivation()) {
                for (auto & [name, maybeOutPath] : queryPartialDerivationOutputMap(*storePath))
                    if (maybeOutPath &&
                        isValidPath(*maybeOutPath) &&
                        queryPathInfo(*maybeOutPath)->deriver == *storePath)
                        incoming.insert(*maybeOutPath);
            }

            /* If keep-outputs is set, then don't delete this path if there
               are derivers of this path that are not garbage. */
            if (state.gcKeepOutputs) {
                auto derivers = queryValidDerivers(*storePath);
                for (auto & i : derivers)
                    incoming.insert(i);
            }

            for (auto & i : incoming)
                if (i != *storePath
                    && (recursive || state.options.pathsToDelete.count(i))
                    && !tryToDelete(state, visited, printStorePath(i), recursive))
                {
                    state.alive.insert(*storePath);
                    return false;
                }
        }

        {
            auto hashPart = std::string(storePath->hashPart());
            auto shared(state.shared.lock());
            if (shared->tempRoots.count(hashPart))
                return false;
            shared->pending = hashPart;
        }

        state.dead.insert(*storePath);

        if (state.shouldDelete)
            invalidatePathChecked(*storePath);
    }

    if (state.shouldDelete) {
        Path realPath = realStoreDir + "/" + std::string(baseNameOf(path));

        struct stat st;
        if (lstat(realPath.c_str(), &st)) {
            if (errno == ENOENT) return true;
            throw SysError("getting status of %1%", realPath);
        }

        printInfo("deleting '%1%'", path);

        state.results.paths.insert(path);

        uint64_t bytesFreed;
        deletePath(realPath, bytesFreed);
        state.results.bytesFreed += bytesFreed;

        if (state.results.bytesFreed > state.options.maxFreed) {
            printInfo("deleted more than %d bytes; stopping", state.options.maxFreed);
            throw GCLimitReached();
        }
    }

    return true;
}


/* Unlink all files in /nix/store/.links that have a link count of 1,
   which indicates that there are no other links and so they can be
   safely deleted.  FIXME: race condition with optimisePath(): we
   might see a link count of 1 just before optimisePath() increases
   the link count. */
void LocalStore::removeUnusedLinks(const GCState & state)
{
    AutoCloseDir dir(opendir(linksDir.c_str()));
    if (!dir) throw SysError("opening directory '%1%'", linksDir);

    int64_t actualSize = 0, unsharedSize = 0;

    struct dirent * dirent;
    while (errno = 0, dirent = readdir(dir.get())) {
        checkInterrupt();
        string name = dirent->d_name;
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

        state.results.bytesFreed += st.st_size;
    }

    struct stat st;
    if (stat(linksDir.c_str(), &st) == -1)
        throw SysError("statting '%1%'", linksDir);
    int64_t overhead = st.st_blocks * 512ULL;

    printInfo("note: currently hard linking saves %.2f MiB",
        ((unsharedSize - actualSize - overhead) / (1024.0 * 1024.0)));
}


void LocalStore::collectGarbage(const GCOptions & options, GCResults & results)
{
    GCState state(options, results);
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

    /* Acquire the global GC root. */
    FdLock gcLock(_state.lock()->fdGCLock.get(), ltWrite, true, "waiting for the big garbage collector lock...");

    /* Start the server for receiving new roots. */
    auto socketPath = stateDir.get() + gcSocketPath;
    createDirs(dirOf(socketPath));
    auto fdServer = createUnixDomainSocket(socketPath, 0666);

    if (fcntl(fdServer.get(), F_SETFL, fcntl(fdServer.get(), F_GETFL) | O_NONBLOCK) == -1)
        throw SysError("making socket '%1%' non-blocking", socketPath);

    Pipe shutdownPipe;
    shutdownPipe.create();

    std::thread serverThread([&]() {
        std::map<int, std::pair<std::unique_ptr<AutoCloseFD>, std::string>> fdClients;
        bool quit = false;

        while (!quit) {
            std::vector<struct pollfd> fds;
            fds.push_back({.fd = shutdownPipe.readSide.get(), .events = POLLIN});
            fds.push_back({.fd = fdServer.get(), .events = POLLIN});
            for (auto & i : fdClients)
                fds.push_back({.fd = i.first, .events = POLLIN});
            auto count = poll(fds.data(), fds.size(), -1);
            assert(count != -1);

            for (auto & fd : fds) {
                if (!fd.revents) continue;
                if (fd.fd == shutdownPipe.readSide.get())
                    /* Parent is asking us to quit. */
                    quit = true;
                else if (fd.fd == fdServer.get()) {
                    /* Accept a new connection. */
                    assert(fd.revents & POLLIN);
                    auto fdClient = std::make_unique<AutoCloseFD>(accept(fdServer.get(), nullptr, nullptr));
                    if (*fdClient) {
                        auto fd = fdClient->get();
                        fdClients.insert({fd, std::make_pair(std::move(fdClient), "")});
                    }
                }
                else {
                    /* Receive data from a client. */
                    auto fdClient = fdClients.find(fd.fd);
                    assert(fdClient != fdClients.end());
                    if (fd.revents & POLLIN) {
                        char buf[16384];
                        auto n = read(fd.fd, buf, sizeof(buf));
                        if (n > 0) {
                            fdClient->second.second.append(buf, n);
                            /* Split the input into lines. */
                            while (true) {
                                auto p = fdClient->second.second.find('\n');
                                if (p == std::string::npos) break;
                                /* We got a full line. Send ack back
                                   to the client. */
                                auto path = fdClient->second.second.substr(0, p);
                                fdClient->second.second = fdClient->second.second.substr(p + 1);
                                auto storePath = maybeParseStorePath(path);
                                if (storePath) {
                                    debug("got new GC root '%s'", path);
                                    auto hashPart = std::string(storePath->hashPart());
                                    auto shared(state.shared.lock());
                                    shared->tempRoots.insert(hashPart);
                                    /* If this path is currently being
                                       deleted, then we have to wait
                                       until deletion is finished to
                                       ensure that the client doesn't
                                       start re-creating it before
                                       we're done. FIXME: ideally we
                                       would use a FD for this so we
                                       don't block the poll loop. */
                                    while (shared->pending == hashPart) {
                                        debug("synchronising with deletion of path '%s'", path);
                                        shared.wait(state.wakeup);
                                    }
                                } else
                                    printError("received garbage instead of a root from client");
                                // This could block, but meh.
                                try {
                                    writeFull(fd.fd, "1", false);
                                } catch (SysError &) { }
                            }
                        } else if (n == 0)
                            fdClients.erase(fdClient);
                    } else
                        fdClients.erase(fdClient);
                }
            }
        }

        debug("GC roots server shut down");
    });

    Finally stopServer([&]() {
        writeFull(shutdownPipe.writeSide.get(), "x", false);
        state.wakeup.notify_all();
        if (serverThread.joinable()) serverThread.join();
    });

    /* Find the roots.  Since we've grabbed the GC lock, the set of
       permanent roots cannot increase now. */
    printInfo("finding garbage collector roots...");
    Roots rootMap;
    if (!options.ignoreLiveness)
        findRootsNoTemp(rootMap, true);

    for (auto & i : rootMap) state.roots.insert(i.first);

    /* Read the temporary roots created before we acquired the global
       GC root. Any new roots will be sent to our socket. */
    Roots tempRoots;
    findTempRoots(tempRoots, true);
    for (auto & root : tempRoots) {
        state.shared.lock()->tempRoots.insert(std::string(root.first.hashPart()));
        state.roots.insert(root.first);
    }

    /* Now either delete all garbage paths, or just the specified
       paths (for gcDeleteSpecific). */

    if (options.action == GCOptions::gcDeleteSpecific) {

        for (auto & i : options.pathsToDelete) {
            StorePathSet visited;
            if (!tryToDelete(state, visited, printStorePath(i), false))
                throw Error(
                    "cannot delete path '%1%' since it is still alive. "
                    "To find out why, use: "
                    "nix-store --query --roots",
                    printStorePath(i));
        }

    } else if (options.maxFreed > 0) {

        if (state.shouldDelete)
            printInfo("deleting garbage...");
        else
            printInfo("determining live/dead paths...");

        try {
            AutoCloseDir dir(opendir(realStoreDir.get().c_str()));
            if (!dir) throw SysError("opening directory '%1%'", realStoreDir);

            /* Read the store and delete all paths that are invalid or
               unreachable. We don't use readDirectory() here so that
               GCing can start faster. */
            Paths entries;
            struct dirent * dirent;
            while (errno = 0, dirent = readdir(dir.get())) {
                checkInterrupt();
                string name = dirent->d_name;
                if (name == "." || name == "..") continue;
                StorePathSet visited;
                tryToDelete(state, visited, storeDir + "/" + name, true);
            }
        } catch (GCLimitReached & e) {
        }
    }

    if (state.options.action == GCOptions::gcReturnLive) {
        for (auto & i : state.alive)
            state.results.paths.insert(printStorePath(i));
        return;
    }

    if (state.options.action == GCOptions::gcReturnDead) {
        for (auto & i : state.dead)
            state.results.paths.insert(printStorePath(i));
        return;
    }

    /* Clean up the links directory. */
    if (options.action == GCOptions::gcDeleteDead || options.action == GCOptions::gcDeleteSpecific) {
        printInfo("deleting unused links...");
        removeUnusedLinks(state);
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
