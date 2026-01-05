#include "nix/store/derivations.hh"
#include "nix/store/globals.hh"
#include "nix/store/local-store.hh"
#include "nix/store/path.hh"
#include "nix/util/finally.hh"
#include "nix/util/unix-domain-socket.hh"
#include "nix/util/signals.hh"
#include "nix/util/serialise.hh"
#include "nix/util/util.hh"
#include "nix/store/posix-fs-canonicalise.hh"

#include "store-config-private.hh"

#if !defined(__linux__)
// For shelling out to lsof
#  include "nix/util/processes.hh"
#endif

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <boost/regex.hpp>
#include <queue>
#include <thread>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#if HAVE_STATVFS
#  include <sys/statvfs.h>
#endif
#ifndef _WIN32
#  include <poll.h>
#  include <sys/socket.h>
#  include <sys/un.h>
#endif
#include <sys/types.h>
#include <unistd.h>

namespace nix {

static std::string gcSocketPath = "/gc-socket/socket";
static std::string gcRootsDir = "gcroots";

void LocalStore::addIndirectRoot(const Path & path)
{
    std::string hash = hashString(HashAlgorithm::SHA1, path).to_string(HashFormat::Nix32, false);
    Path realRoot = canonPath(fmt("%1%/%2%/auto/%3%", config->stateDir, gcRootsDir, hash));
    makeSymlink(realRoot, path);
}

void LocalStore::createTempRootsFile()
{
    auto fdTempRoots(_fdTempRoots.lock());

    /* Create the temporary roots file for this process. */
    if (*fdTempRoots)
        return;

    while (1) {
        if (pathExists(fnTempRoots))
            /* It *must* be stale, since there can be no two
               processes with the same pid. */
            unlink(fnTempRoots.c_str());

        *fdTempRoots = openLockFile(fnTempRoots, true);

        debug("acquiring write lock on '%s'", fnTempRoots);
        lockFile(fdTempRoots->get(), ltWrite, true);

        /* Check whether the garbage collector didn't get in our
           way. */
        struct stat st;
        if (fstat(fromDescriptorReadOnly(fdTempRoots->get()), &st) == -1)
            throw SysError("statting '%1%'", fnTempRoots);
        if (st.st_size == 0)
            break;

        /* The garbage collector deleted this file before we could get
           a lock.  (It won't delete the file after we get a lock.)
           Try again. */
    }
}

void LocalStore::addTempRoot(const StorePath & path)
{
    if (config->readOnly) {
        debug(
            "Read-only store doesn't support creating lock files for temp roots, but nothing can be deleted anyways.");
        return;
    }

    createTempRootsFile();

    /* Open/create the global GC lock file. */
    {
        auto fdGCLock(_fdGCLock.lock());
        if (!*fdGCLock)
            *fdGCLock = openGCLock();
    }

restart:
    /* Try to acquire a shared global GC lock (non-blocking). This
       only succeeds if the garbage collector is not currently
       running. */
    FdLock gcLock(_fdGCLock.lock()->get(), ltRead, false, "");

    if (!gcLock.acquired) {
        /* We couldn't get a shared global GC lock, so the garbage
           collector is running. So we have to connect to the garbage
           collector and inform it about our root. */
        auto fdRootsSocket(_fdRootsSocket.lock());

        if (!*fdRootsSocket) {
            auto socketPath = config->stateDir.get() + gcSocketPath;
            debug("connecting to '%s'", socketPath);
            *fdRootsSocket = createUnixDomainSocket();
            try {
                nix::connect(toSocket(fdRootsSocket->get()), socketPath);
            } catch (SysError & e) {
                /* The garbage collector may have exited or not
                   created the socket yet, so we need to restart. */
                if (e.errNo == ECONNREFUSED || e.errNo == ENOENT) {
                    debug("GC socket connection refused: %s", e.msg());
                    fdRootsSocket->close();
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    goto restart;
                }
                throw;
            }
        }

        try {
            debug("sending GC root '%s'", printStorePath(path));
            writeFull(fdRootsSocket->get(), printStorePath(path) + "\n", false);
            char c;
            readFull(fdRootsSocket->get(), &c, 1);
            assert(c == '1');
            debug("got ack for GC root '%s'", printStorePath(path));
        } catch (SysError & e) {
            /* The garbage collector may have exited, so we need to
               restart. */
            if (e.errNo == EPIPE || e.errNo == ECONNRESET) {
                debug("GC socket disconnected");
                fdRootsSocket->close();
                goto restart;
            }
            throw;
        } catch (EndOfFile & e) {
            debug("GC socket disconnected");
            fdRootsSocket->close();
            goto restart;
        }
    }

    /* Record the store path in the temporary roots file so it will be
       seen by a future run of the garbage collector. */
    auto s = printStorePath(path) + '\0';
    writeFull(_fdTempRoots.lock()->get(), s);
}

static std::string censored = "{censored}";

void LocalStore::findTempRoots(Roots & tempRoots, bool censor)
{
    /* Read the `temproots' directory for per-process temporary root
       files. */
    for (auto & i : DirectoryIterator{tempRootsDir}) {
        checkInterrupt();
        auto name = i.path().filename().string();
        if (name[0] == '.') {
            // Ignore hidden files. Some package managers (notably portage) create
            // those to keep the directory alive.
            continue;
        }
        Path path = i.path().string();

        pid_t pid = std::stoi(name);

        debug("reading temporary root file '%1%'", path);
        AutoCloseFD fd(toDescriptor(open(
            path.c_str(),
#ifndef _WIN32
            O_CLOEXEC |
#endif
                O_RDWR,
            0666)));
        if (!fd) {
            /* It's okay if the file has disappeared. */
            if (errno == ENOENT)
                continue;
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

void LocalStore::findRoots(const Path & path, std::filesystem::file_type type, Roots & roots)
{
    auto foundRoot = [&](const Path & path, const Path & target) {
        try {
            auto storePath = toStorePath(target).first;
            if (isValidPath(storePath))
                roots[std::move(storePath)].emplace(path);
            else
                printInfo("skipping invalid root from '%1%' to '%2%'", path, target);
        } catch (BadStorePath &) {
        }
    };

    try {

        if (type == std::filesystem::file_type::unknown)
            type = std::filesystem::symlink_status(path).type();

        if (type == std::filesystem::file_type::directory) {
            for (auto & i : DirectoryIterator{path}) {
                checkInterrupt();
                findRoots(i.path().string(), i.symlink_status().type(), roots);
            }
        }

        else if (type == std::filesystem::file_type::symlink) {
            Path target = readLink(path);
            if (isInStore(target))
                foundRoot(path, target);

            /* Handle indirect roots. */
            else {
                target = absPath(target, dirOf(path));
                if (!pathExists(target)) {
                    if (isInDir(path, std::filesystem::path{config->stateDir.get()} / gcRootsDir / "auto")) {
                        printInfo("removing stale link from '%1%' to '%2%'", path, target);
                        unlink(path.c_str());
                    }
                } else {
                    if (!std::filesystem::is_symlink(target))
                        return;
                    Path target2 = readLink(target);
                    if (isInStore(target2))
                        foundRoot(target, target2);
                }
            }
        }

        else if (type == std::filesystem::file_type::regular) {
            auto storePath = maybeParseStorePath(storeDir + "/" + std::string(baseNameOf(path)));
            if (storePath && isValidPath(*storePath))
                roots[std::move(*storePath)].emplace(path);
        }

    }

    catch (std::filesystem::filesystem_error & e) {
        /* We only ignore permanent failures. */
        if (e.code() == std::errc::permission_denied || e.code() == std::errc::no_such_file_or_directory
            || e.code() == std::errc::not_a_directory)
            printInfo("cannot read potential root '%1%'", path);
        else
            throw;
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
    findRoots(config->stateDir + "/" + gcRootsDir, std::filesystem::file_type::unknown, roots);
    findRoots(config->stateDir + "/profiles", std::filesystem::file_type::unknown, roots);

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

/**
 * Key is a mere string because cannot has path with macOS's libc++
 */
typedef boost::unordered_flat_map<
    std::string,
    boost::unordered_flat_set<std::string, StringViewHash, std::equal_to<>>,
    StringViewHash,
    std::equal_to<>>
    UncheckedRoots;

static void readProcLink(const std::filesystem::path & file, UncheckedRoots & roots)
{
    std::filesystem::path buf;
    try {
        buf = std::filesystem::read_symlink(file);
    } catch (std::filesystem::filesystem_error & e) {
        if (e.code() == std::errc::no_such_file_or_directory || e.code() == std::errc::permission_denied
            || e.code() == std::errc::no_such_process)
            return;
        throw;
    }
    if (buf.is_absolute())
        roots[buf.string()].emplace(file.string());
}

static std::string quoteRegexChars(const std::string & raw)
{
    static auto specialRegex = boost::regex(R"([.^$\\*+?()\[\]{}|])");
    return boost::regex_replace(raw, specialRegex, R"(\$&)");
}

#ifdef __linux__
static void readFileRoots(const std::filesystem::path & path, UncheckedRoots & roots)
{
    try {
        roots[readFile(path)].emplace(path.string());
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
        static const auto digitsRegex = boost::regex(R"(^\d+$)");
        static const auto mapRegex = boost::regex(R"(^\s*\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+(/\S+)\s*$)");
        auto storePathRegex = boost::regex(quoteRegexChars(storeDir) + R"(/[0-9a-z]+[0-9a-zA-Z\+\-\._\?=]*)");
        while (errno = 0, ent = readdir(procDir.get())) {
            checkInterrupt();
            if (boost::regex_match(ent->d_name, digitsRegex)) {
                try {
                    readProcLink(fmt("/proc/%s/exe", ent->d_name), unchecked);
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

                    std::filesystem::path mapFile = fmt("/proc/%s/maps", ent->d_name);
                    auto mapLines = tokenizeString<std::vector<std::string>>(readFile(mapFile.string()), "\n");
                    for (const auto & line : mapLines) {
                        auto match = boost::smatch{};
                        if (boost::regex_match(line, match, mapRegex))
                            unchecked[match[1]].emplace(mapFile.string());
                    }

                    auto envFile = fmt("/proc/%s/environ", ent->d_name);
                    auto envString = readFile(envFile);
                    auto env_end = boost::sregex_iterator{};
                    for (auto i = boost::sregex_iterator{envString.begin(), envString.end(), storePathRegex};
                         i != env_end;
                         ++i)
                        unchecked[i->str()].emplace(envFile);
                } catch (SystemError & e) {
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
            boost::regex lsofRegex(R"(^n(/.*)$)");
            auto lsofLines =
                tokenizeString<std::vector<std::string>>(runProgram(LSOF, true, {"-n", "-w", "-F", "n"}), "\n");
            for (const auto & line : lsofLines) {
                boost::smatch match;
                if (boost::regex_match(line, match, lsofRegex))
                    unchecked[match[1].str()].emplace("{lsof}");
            }
        } catch (ExecError & e) {
            /* lsof not installed, lsof failed */
        }
    }
#endif

#ifdef __linux__
    readFileRoots("/proc/sys/kernel/modprobe", unchecked);
    readFileRoots("/proc/sys/kernel/fbsplash", unchecked);
    readFileRoots("/proc/sys/kernel/poweroff_cmd", unchecked);
#endif

    for (auto & [target, links] : unchecked) {
        if (!isInStore(target))
            continue;
        try {
            auto path = toStorePath(target).first;
            if (!isValidPath(path))
                continue;
            debug("got additional root '%1%'", printStorePath(path));
            if (censor)
                roots[path].insert(censored);
            else
                roots[path].insert(links.begin(), links.end());
        } catch (BadStorePath &) {
        }
    }
}

struct GCLimitReached
{};

void LocalStore::collectGarbage(const GCOptions & options, GCResults & results)
{
    bool shouldDelete = options.action == GCOptions::gcDeleteDead || options.action == GCOptions::gcDeleteSpecific;
    bool gcKeepOutputs = settings.gcKeepOutputs;
    bool gcKeepDerivations = settings.gcKeepDerivations;

    boost::unordered_flat_set<StorePath, std::hash<StorePath>> roots, dead, alive;

    struct Shared
    {
        // The temp roots only store the hash part to make it easier to
        // ignore suffixes like '.lock', '.chroot' and '.check'.
        boost::unordered_flat_set<std::string, StringViewHash, std::equal_to<>> tempRoots;

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

    /* Synchronisation point to test ENOENT handling in
       addTempRoot(), see tests/gc-non-blocking.sh. */
    if (auto p = getEnv("_NIX_TEST_GC_SYNC_1"))
        readFile(*p);

    /* Start the server for receiving new roots. */
    auto socketPath = config->stateDir.get() + gcSocketPath;
    createDirs(dirOf(socketPath));
    auto fdServer = createUnixDomainSocket(socketPath, 0666);

    // TODO nonblocking socket on windows?
#ifdef _WIN32
    throw UnimplementedError("External GC client not implemented yet");
#else
    if (fcntl(fdServer.get(), F_SETFL, fcntl(fdServer.get(), F_GETFL) | O_NONBLOCK) == -1)
        throw SysError("making socket '%1%' non-blocking", socketPath);

    Pipe shutdownPipe;
    shutdownPipe.create();

    std::thread serverThread([&]() {
        Sync<std::map<int, std::thread>> connections;

        Finally cleanup([&]() {
            debug("GC roots server shutting down");
            fdServer.close();
            while (true) {
                auto item = remove_begin(*connections.lock());
                if (!item)
                    break;
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
                if (!fdClient)
                    continue;

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
                    if (fcntl(fdClient.get(), F_SETFL, fcntl(fdClient.get(), F_GETFL) & ~O_NONBLOCK) == -1)
                        panic("Could not set non-blocking flag on client socket");

                    FdSource source(fdClient.get());
                    while (true) {
                        try {
                            auto path = source.readLine();
                            auto storePath = maybeParseStorePath(path);
                            if (storePath) {
                                debug("got new GC root '%s'", path);
                                auto hashPart = storePath->hashPart();
                                auto shared(_shared.lock());
                                shared->tempRoots.emplace(hashPart);
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
        if (serverThread.joinable())
            serverThread.join();
    });

#endif

    /* Find the roots.  Since we've grabbed the GC lock, the set of
       permanent roots cannot increase now. */
    printInfo("finding garbage collector roots...");
    Roots rootMap;
    if (!options.ignoreLiveness)
        findRootsNoTemp(rootMap, true);

    for (auto & i : rootMap)
        roots.insert(i.first);

    /* Read the temporary roots created before we acquired the global
       GC root. Any new roots will be sent to our socket. */
    Roots tempRoots;
    findTempRoots(tempRoots, true);
    for (auto & root : tempRoots) {
        _shared.lock()->tempRoots.emplace(root.first.hashPart());
        roots.insert(root.first);
    }

    /* Synchronisation point for testing, see tests/functional/gc-non-blocking.sh. */
    if (auto p = getEnv("_NIX_TEST_GC_SYNC_2"))
        readFile(*p);

    /* Helper function that deletes a path from the store and throws
       GCLimitReached if we've deleted enough garbage. */
    auto deleteFromStore = [&](std::string_view baseName) {
        Path path = storeDir + "/" + std::string(baseName);
        Path realPath = config->realStoreDir + "/" + std::string(baseName);

        /* There may be temp directories in the store that are still in use
           by another process. We need to be sure that we can acquire an
           exclusive lock before deleting them. */
        if (baseName.find("tmp-", 0) == 0) {
            AutoCloseFD tmpDirFd = openDirectory(realPath);
            if (!tmpDirFd || !lockFile(tmpDirFd.get(), ltWrite, false)) {
                debug("skipping locked tempdir '%s'", realPath);
                return;
            }
        }

        printInfo("deleting '%1%'", path);

        results.paths.insert(path);

        uint64_t bytesFreed;
        deleteStorePath(realPath, bytesFreed);

        results.bytesFreed += bytesFreed;

        if (results.bytesFreed > options.maxFreed) {
            printInfo("deleted more than %d bytes; stopping", options.maxFreed);
            throw GCLimitReached();
        }
    };

    boost::unordered_flat_map<StorePath, StorePathSet, std::hash<StorePath>> referrersCache;

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
            if (dead.count(*path))
                continue;

            auto markAlive = [&]() {
                alive.insert(*path);
                alive.insert(start);
                try {
                    StorePathSet closure;
                    computeFSClosure(
                        *path,
                        closure,
                        /* flipDirection */ false,
                        gcKeepOutputs,
                        gcKeepDerivations);
                    for (auto & p : closure)
                        alive.insert(p);
                } catch (InvalidPath &) {
                }
            };

            /* If this is a root, bail out. */
            if (roots.count(*path)) {
                debug("cannot delete '%s' because it's a root", printStorePath(*path));
                return markAlive();
            }

            if (options.action == GCOptions::gcDeleteSpecific && !options.pathsToDelete.count(*path))
                return;

            {
                auto hashPart = path->hashPart();
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
                    queryGCReferrers(*path, referrers);
                    referrersCache.emplace(*path, std::move(referrers));
                    i = referrersCache.find(*path);
                }
                for (auto & p : i->second)
                    enqueue(p);

                /* If keep-derivations is set and this is a
                   derivation, then visit the derivation outputs. */
                if (gcKeepDerivations && path->isDerivation()) {
                    for (auto & [name, maybeOutPath] : queryPartialDerivationOutputMap(*path))
                        if (maybeOutPath && isValidPath(*maybeOutPath)
                            && queryPathInfo(*maybeOutPath)->deriver == *path)
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
            if (!dead.insert(path).second)
                continue;
            if (shouldDelete) {
                try {
                    invalidatePathChecked(path);
                    deleteFromStore(path.to_string());
                    referrersCache.erase(path);
                } catch (PathInUse & e) {
                    // If we end up here, it's likely a new occurrence
                    // of https://github.com/NixOS/nix/issues/11923
                    printError("BUG: %s", e.what());
                }
            }
        }
    };

    /* Either delete all garbage paths, or just the specified
       paths (for gcDeleteSpecific). */
    if (options.action == GCOptions::gcDeleteSpecific) {

        for (auto & i : options.pathsToDelete) {
            deleteReferrersClosure(i);
            if (!dead.count(i))
                throw Error(
                    "Cannot delete path '%1%' since it is still alive. "
                    "To find out why, use: "
                    "nix-store --query --roots and nix-store --query --referrers",
                    printStorePath(i));
        }

    } else if (options.maxFreed > 0) {

        if (shouldDelete)
            printInfo("deleting garbage...");
        else
            printInfo("determining live/dead paths...");

        try {
            AutoCloseDir dir(opendir(config->realStoreDir.get().c_str()));
            if (!dir)
                throw SysError("opening directory '%1%'", config->realStoreDir);

            /* Read the store and delete all paths that are invalid or
               unreachable. We don't use readDirectory() here so that
               GCing can start faster. */
            auto linksName = baseNameOf(linksDir);
            struct dirent * dirent;
            while (errno = 0, dirent = readdir(dir.get())) {
                checkInterrupt();
                std::string name = dirent->d_name;
                if (name == "." || name == ".." || name == linksName)
                    continue;

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
        if (!dir)
            throw SysError("opening directory '%1%'", linksDir);

        int64_t actualSize = 0, unsharedSize = 0;

        struct dirent * dirent;
        while (errno = 0, dirent = readdir(dir.get())) {
            checkInterrupt();
            std::string name = dirent->d_name;
            if (name == "." || name == "..")
                continue;
            Path path = linksDir + "/" + name;

            auto st = lstat(path);

            if (st.st_nlink != 1) {
                actualSize += st.st_size;
                unsharedSize += (st.st_nlink - 1) * st.st_size;
                continue;
            }

            printMsg(lvlTalkative, "deleting unused link '%1%'", path);

            if (unlink(path.c_str()) == -1)
                throw SysError("deleting '%1%'", path);

            /* Do not account for deleted file here. Rely on deletePath()
               accounting.  */
        }

        struct stat st;
        if (stat(linksDir.c_str(), &st) == -1)
            throw SysError("statting '%1%'", linksDir);
        int64_t overhead =
#ifdef _WIN32
            0
#else
            st.st_blocks * 512ULL
#endif
            ;

        printInfo("note: hard linking is currently saving %s", renderSize(unsharedSize - actualSize - overhead));
    }

    /* While we're at it, vacuum the database. */
    // if (options.action == GCOptions::gcDeleteDead) vacuumDB();
}

void LocalStore::autoGC(bool sync)
{
#if HAVE_STATVFS
    static auto fakeFreeSpaceFile = getEnv("_NIX_TEST_FREE_SPACE_FILE");

    auto getAvail = [this]() -> uint64_t {
        if (fakeFreeSpaceFile)
            return std::stoll(readFile(*fakeFreeSpaceFile));

        struct statvfs st;
        if (statvfs(config->realStoreDir.get().c_str(), &st))
            throw SysError("getting filesystem info about '%s'", config->realStoreDir);

        return (uint64_t) st.f_bavail * st.f_frsize;
    };

    std::shared_future<void> future;

    {
        auto state(_state->lock());

        if (state->gcRunning) {
            future = state->gcFuture;
            debug("waiting for auto-GC to finish");
            goto sync;
        }

        auto now = std::chrono::steady_clock::now();

        if (now < state->lastGCCheck + std::chrono::seconds(settings.minFreeCheckInterval))
            return;

        auto avail = getAvail();

        state->lastGCCheck = now;

        if (avail >= settings.minFree || avail >= settings.maxFree)
            return;

        if (avail > state->availAfterGC * 0.97)
            return;

        state->gcRunning = true;

        std::promise<void> promise;
        future = state->gcFuture = promise.get_future().share();

        std::thread([promise{std::move(promise)}, this, avail, getAvail]() mutable {
            try {

                /* Wake up any threads waiting for the auto-GC to finish. */
                Finally wakeup([&]() {
                    auto state(_state->lock());
                    state->gcRunning = false;
                    state->lastGCCheck = std::chrono::steady_clock::now();
                    promise.set_value();
                });

                GCOptions options;
                options.maxFreed = settings.maxFree - avail;

                printInfo("running auto-GC to free %d bytes", options.maxFreed);

                GCResults results;

                collectGarbage(options, results);

                _state->lock()->availAfterGC = getAvail();

            } catch (...) {
                // FIXME: we could propagate the exception to the
                // future, but we don't really care. (what??)
                ignoreExceptionInDestructor();
            }
        }).detach();
    }

sync:
    // Wait for the future outside of the state lock.
    if (sync)
        future.get();
#endif
}

} // namespace nix
