#include "serialise.hh"
#include "util.hh"
#include "remote-store.hh"
#include "worker-protocol.hh"
#include "archive.hh"
#include "affinity.hh"
#include "globals.hh"
#include "pathlocks.hh"
#include "derivations.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>

#include <iostream>
#include <unistd.h>
#include <cstring>

namespace nix {


Path readStorePath(Source & from)
{
    Path path = readString(from);
    assertStorePath(path);
    return path;
}


template<class T> T readStorePaths(Source & from)
{
    T paths = readStrings<T>(from);
    foreach (typename T::iterator, i, paths) assertStorePath(*i);
    return paths;
}

template PathSet readStorePaths(Source & from);


RemoteStore::RemoteStore()
{
    initialised = false;
}


void RemoteStore::openConnection(bool reserveSpace)
{
    if (initialised) return;
    initialised = true;

    string remoteMode = getEnv("NIX_REMOTE");

    if (remoteMode == "daemon")
        /* Connect to a daemon that does the privileged work for
           us. */
        connectToDaemon();
    else if (remoteMode == "recursive") {
        unsigned int daemonRecursiveVersion;
        if (!string2Int(getEnv("NIX_REMOTE_RECURSIVE_PROTOCOL_VERSION"), daemonRecursiveVersion))
            throw Error("expected an integer in NIX_REMOTE_RECURSIVE_PROTOCOL_VERSION");

        if (GET_PROTOCOL_MAJOR(daemonRecursiveVersion) != GET_PROTOCOL_MAJOR(RECURSIVE_PROTOCOL_VERSION))
            throw Error("nix daemon recursive protocol version not supported");

        int sfd;
        if (!string2Int(getEnv("NIX_REMOTE_RECURSIVE_SOCKET_FD"), sfd))
            throw Error("expected an fd in NIX_REMOTE_RECURSIVE_SOCKET_FD");

        int fds[2];
        if (socketpair(PF_UNIX, SOCK_STREAM, 0, fds) == -1)
            throw SysError("creating recursive daemon socketpair");

        /* Send our protocol version and a socket to the daemon */
        unsigned char buf[sizeof RECURSIVE_PROTOCOL_VERSION];
        for (size_t byte = 0; byte < sizeof buf; ++byte)
            buf[byte] =
                (unsigned char) ((RECURSIVE_PROTOCOL_VERSION >> (8 * byte)) & 0xFF);
        struct iovec iov;
        iov.iov_base = buf;
        iov.iov_len = sizeof buf;
        struct msghdr msg;
        memset(&msg, 0, sizeof msg);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        char data[CMSG_SPACE(sizeof fds[1])];
        msg.msg_control = data;
        msg.msg_controllen = sizeof data;
        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_len = CMSG_LEN(sizeof fds[1]);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        memmove(CMSG_DATA(cmsg), &fds[1], sizeof fds[1]);
        ssize_t count = sendmsg(sfd, &msg, 0);
        if (count == -1)
            throw SysError("sending socket descriptor to daemon");
        else if ((size_t) count != sizeof buf)
            throw Error(format("tried to send %1% bytes to the daemon, but only %2% were sent")
                    % sizeof buf % count);
        close(fds[1]);

        fdSocket = fds[0];

        int pathsFd;
        if (!string2Int(getEnv("NIX_REMOTE_RECURSIVE_PATHS_FD"), pathsFd))
            throw Error("expected an fd in NIX_REMOTE_RECURSIVE_PATHS_FD");
        fdRecursivePaths = pathsFd;
    } else
        throw Error(format("invalid setting for NIX_REMOTE, `%1%'") % remoteMode);

    closeOnExec(fdSocket);

    from.fd = fdSocket;
    to.fd = fdSocket;

    /* Send the magic greeting, check for the reply. */
    try {
        writeInt(WORKER_MAGIC_1, to);
        to.flush();
        unsigned int magic = readInt(from);
        if (magic != WORKER_MAGIC_2) throw Error("protocol mismatch");

        daemonVersion = readInt(from);
        if (GET_PROTOCOL_MAJOR(daemonVersion) != GET_PROTOCOL_MAJOR(PROTOCOL_VERSION))
            throw Error("Nix daemon protocol version not supported");
        writeInt(PROTOCOL_VERSION, to);

        if (GET_PROTOCOL_MINOR(daemonVersion) >= 14) {
            int cpu = settings.lockCPU ? lockToCurrentCPU() : -1;
            if (cpu != -1) {
                writeInt(1, to);
                writeInt(cpu, to);
            } else
                writeInt(0, to);
        }

        if (GET_PROTOCOL_MINOR(daemonVersion) >= 11)
            writeInt(reserveSpace, to);

        processStderr();
    }
    catch (Error & e) {
        throw Error(format("cannot start worker (%1%)")
            % e.msg());
    }

    setOptions();
}


void RemoteStore::connectToDaemon()
{
    fdSocket = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fdSocket == -1)
        throw SysError("cannot create Unix domain socket");

    string socketPath = settings.nixDaemonSocketFile;

    /* Urgh, sockaddr_un allows path names of only 108 characters.  So
       chdir to the socket directory so that we can pass a relative
       path name.  !!! this is probably a bad idea in multi-threaded
       applications... */
    AutoCloseFD fdPrevDir = open(".", O_RDONLY);
    if (fdPrevDir == -1) throw SysError("couldn't open current directory");
    chdir(dirOf(socketPath).c_str());
    Path socketPathRel = "./" + baseNameOf(socketPath);

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    if (socketPathRel.size() >= sizeof(addr.sun_path))
        throw Error(format("socket path `%1%' is too long") % socketPathRel);
    using namespace std;
    strcpy(addr.sun_path, socketPathRel.c_str());

    if (connect(fdSocket, (struct sockaddr *) &addr, sizeof(addr)) == -1)
        throw SysError(format("cannot connect to daemon at `%1%'") % socketPath);

    if (fchdir(fdPrevDir) == -1)
        throw SysError("couldn't change back to previous directory");
}


RemoteStore::~RemoteStore()
{
    try {
        to.flush();
        fdSocket.close();
        if (child != -1)
            child.wait(true);
    } catch (...) {
        ignoreException();
    }
}


void RemoteStore::setOptions()
{
    writeInt(wopSetOptions, to);

    writeInt(settings.keepFailed, to);
    writeInt(settings.keepGoing, to);
    writeInt(settings.tryFallback, to);
    writeInt(verbosity, to);
    writeInt(settings.maxBuildJobs, to);
    writeInt(settings.maxSilentTime, to);
    if (GET_PROTOCOL_MINOR(daemonVersion) >= 2)
        writeInt(settings.useBuildHook, to);
    if (GET_PROTOCOL_MINOR(daemonVersion) >= 4) {
        writeInt(settings.buildVerbosity, to);
        writeInt(logType, to);
        writeInt(settings.printBuildTrace, to);
    }
    if (GET_PROTOCOL_MINOR(daemonVersion) >= 6)
        writeInt(settings.buildCores, to);
    if (GET_PROTOCOL_MINOR(daemonVersion) >= 10)
        writeInt(settings.useSubstitutes, to);

    if (GET_PROTOCOL_MINOR(daemonVersion) >= 12) {
        Settings::SettingsMap overrides = settings.getOverrides();
        writeInt(overrides.size(), to);
        foreach (Settings::SettingsMap::iterator, i, overrides) {
            writeString(i->first, to);
            writeString(i->second, to);
        }
    }

    processStderr();
}


bool RemoteStore::isValidPath(const Path & path)
{
    openConnection();
    writeInt(wopIsValidPath, to);
    writeString(path, to);
    processStderr();
    unsigned int reply = readInt(from);
    return reply != 0;
}


PathSet RemoteStore::queryValidPaths(const PathSet & paths)
{
    openConnection();
    if (GET_PROTOCOL_MINOR(daemonVersion) < 12) {
        PathSet res;
        foreach (PathSet::const_iterator, i, paths)
            if (isValidPath(*i)) res.insert(*i);
        return res;
    } else {
        writeInt(wopQueryValidPaths, to);
        writeStrings(paths, to);
        processStderr();
        return readStorePaths<PathSet>(from);
    }
}


PathSet RemoteStore::queryAllValidPaths()
{
    openConnection();
    writeInt(wopQueryAllValidPaths, to);
    processStderr();
    PathSet res = readStorePaths<PathSet>(from);
    reportRecursivePaths(res);
    return res;
}


PathSet RemoteStore::querySubstitutablePaths(const PathSet & paths)
{
    openConnection();
    if (GET_PROTOCOL_MINOR(daemonVersion) < 12) {
        PathSet res;
        foreach (PathSet::const_iterator, i, paths) {
            writeInt(wopHasSubstitutes, to);
            writeString(*i, to);
            processStderr();
            if (readInt(from)) res.insert(*i);
        }
        return res;
    } else {
        writeInt(wopQuerySubstitutablePaths, to);
        writeStrings(paths, to);
        processStderr();
        return readStorePaths<PathSet>(from);
    }
}


void RemoteStore::querySubstitutablePathInfos(const PathSet & paths,
    SubstitutablePathInfos & infos)
{
    if (paths.empty()) return;

    openConnection();

    if (GET_PROTOCOL_MINOR(daemonVersion) < 3) return;

    PathSet recursivePaths;

    if (GET_PROTOCOL_MINOR(daemonVersion) < 12) {

        foreach (PathSet::const_iterator, i, paths) {
            SubstitutablePathInfo info;
            writeInt(wopQuerySubstitutablePathInfo, to);
            writeString(*i, to);
            processStderr();
            unsigned int reply = readInt(from);
            if (reply == 0) continue;
            info.deriver = readString(from);
            if (info.deriver != "") {
                assertStorePath(info.deriver);
                recursivePaths.insert(info.deriver);
            }
            info.references = readStorePaths<PathSet>(from);
            recursivePaths.insert(info.references.begin(), info.references.end());
            info.downloadSize = readLongLong(from);
            info.narSize = GET_PROTOCOL_MINOR(daemonVersion) >= 7 ? readLongLong(from) : 0;
            infos[*i] = info;
        }

    } else {

        writeInt(wopQuerySubstitutablePathInfos, to);
        writeStrings(paths, to);
        processStderr();
        unsigned int count = readInt(from);
        for (unsigned int n = 0; n < count; n++) {
            Path path = readStorePath(from);
            SubstitutablePathInfo & info(infos[path]);
            info.deriver = readString(from);
            if (info.deriver != "") {
                assertStorePath(info.deriver);
                recursivePaths.insert(info.deriver);
            }
            info.references = readStorePaths<PathSet>(from);
            recursivePaths.insert(info.references.begin(), info.references.end());
            info.downloadSize = readLongLong(from);
            info.narSize = readLongLong(from);
        }

    }

    reportRecursivePaths(recursivePaths);
}


ValidPathInfo RemoteStore::queryPathInfo(const Path & path)
{
    openConnection();
    writeInt(wopQueryPathInfo, to);
    writeString(path, to);
    processStderr();
    ValidPathInfo info;
    info.path = path;
    info.deriver = readString(from);
    PathSet recursivePaths;
    if (info.deriver != "") {
        assertStorePath(info.deriver);
        recursivePaths.insert(info.deriver);
    }
    info.hash = parseHash(htSHA256, readString(from));
    info.references = readStorePaths<PathSet>(from);
    recursivePaths.insert(info.references.begin(), info.references.end());
    info.registrationTime = readInt(from);
    info.narSize = readLongLong(from);

    reportRecursivePaths(recursivePaths);
    return info;
}


Hash RemoteStore::queryPathHash(const Path & path)
{
    openConnection();
    writeInt(wopQueryPathHash, to);
    writeString(path, to);
    processStderr();
    string hash = readString(from);
    return parseHash(htSHA256, hash);
}


void RemoteStore::queryReferences(const Path & path,
    PathSet & references)
{
    openConnection();
    writeInt(wopQueryReferences, to);
    writeString(path, to);
    processStderr();
    PathSet references2 = readStorePaths<PathSet>(from);
    reportRecursivePaths(references2);
    references.insert(references2.begin(), references2.end());
}


void RemoteStore::queryReferrers(const Path & path,
    PathSet & referrers)
{
    openConnection();
    writeInt(wopQueryReferrers, to);
    writeString(path, to);
    processStderr();
    PathSet referrers2 = readStorePaths<PathSet>(from);
    /* no need to add to recursive paths, they're brought in automatically */
    referrers.insert(referrers2.begin(), referrers2.end());
}


Path RemoteStore::queryDeriver(const Path & path)
{
    openConnection();
    writeInt(wopQueryDeriver, to);
    writeString(path, to);
    processStderr();
    Path drvPath = readString(from);
    if (drvPath != "") {
        assertStorePath(drvPath);
        reportRecursivePath(drvPath);
    }
    return drvPath;
}


PathSet RemoteStore::queryValidDerivers(const Path & path)
{
    openConnection();
    writeInt(wopQueryValidDerivers, to);
    writeString(path, to);
    processStderr();
    PathSet res = readStorePaths<PathSet>(from);
    reportRecursivePaths(res);
    return res;
}


PathSet RemoteStore::queryDerivationOutputs(const Path & path)
{
    openConnection();
    writeInt(wopQueryDerivationOutputs, to);
    writeString(path, to);
    processStderr();
    /* derivations are treated specially: Unless they became accessible
       via unsafeDiscardOutputDependency, their outputs are treated as
       inputs to scan for even if, in the case of recursive nix, they
       may not be valid. So no need to add them to recursivePaths */
    return readStorePaths<PathSet>(from);
}


PathSet RemoteStore::queryDerivationOutputNames(const Path & path)
{
    openConnection();
    writeInt(wopQueryDerivationOutputNames, to);
    writeString(path, to);
    processStderr();
    return readStrings<PathSet>(from);
}


Path RemoteStore::queryPathFromHashPart(const string & hashPart)
{
    openConnection();
    writeInt(wopQueryPathFromHashPart, to);
    writeString(hashPart, to);
    processStderr();
    Path path = readString(from);
    if (!path.empty()) {
        assertStorePath(path);
        reportRecursivePath(path);
    }
    return path;
}


Path RemoteStore::addToStore(const Path & _srcPath,
    bool recursive, HashType hashAlgo, PathFilter & filter, bool repair)
{
    if (repair) throw Error("repairing is not supported when building through the Nix daemon");

    openConnection();

    Path srcPath(absPath(_srcPath));

    writeInt(wopAddToStore, to);
    writeString(baseNameOf(srcPath), to);
    /* backwards compatibility hack */
    writeInt((hashAlgo == htSHA256 && recursive) ? 0 : 1, to);
    writeInt(recursive ? 1 : 0, to);
    writeString(printHashType(hashAlgo), to);
    dumpPath(srcPath, to, filter);
    processStderr();
    Path res = readStorePath(from);
    reportRecursivePath(res);
    return res;
}


Path RemoteStore::addTextToStore(const string & name, const string & s,
    const PathSet & references, bool repair)
{
    if (repair) throw Error("repairing is not supported when building through the Nix daemon");

    openConnection();
    writeInt(wopAddTextToStore, to);
    writeString(name, to);
    writeString(s, to);
    writeStrings(references, to);

    processStderr();
    Path res = readStorePath(from);
    reportRecursivePath(res);
    return res;
}


void RemoteStore::exportPath(const Path & path, bool sign,
    Sink & sink)
{
    openConnection();
    writeInt(wopExportPath, to);
    writeString(path, to);
    writeInt(sign ? 1 : 0, to);
    processStderr(&sink); /* sink receives the actual data */
    readInt(from);
}


Paths RemoteStore::importPaths(bool requireSignature, Source & source)
{
    openConnection();
    writeInt(wopImportPaths, to);
    /* We ignore requireSignature, since the worker forces it to true
       anyway. */
    processStderr(0, &source);
    Paths res = readStorePaths<Paths>(from);
    reportRecursivePaths(PathSet(res.begin(), res.end()));
    return res;
}


void RemoteStore::buildPaths(const PathSet & drvPaths, BuildMode buildMode)
{
    if (buildMode != bmNormal) throw Error("repairing or checking is not supported when building through the Nix daemon");
    openConnection();
    writeInt(wopBuildPaths, to);
    if (GET_PROTOCOL_MINOR(daemonVersion) >= 13)
        writeStrings(drvPaths, to);
    else {
        /* For backwards compatibility with old daemons, strip output
           identifiers. */
        PathSet drvPaths2;
        foreach (PathSet::const_iterator, i, drvPaths)
            drvPaths2.insert(string(*i, 0, i->find('!')));
        writeStrings(drvPaths2, to);
    }
    processStderr();
    readInt(from);

    /* Report paths again so new ones can be added to chroot */
    PathSet recursivePaths;
    foreach (PathSet::const_iterator, i, drvPaths) {
        DrvPathWithOutputs i2 = parseDrvPathWithOutputs(*i);
        if (isDerivation(i2.first))
            recursivePaths.insert(i2.first);
        else
            recursivePaths.insert(*i);
    }
    reportRecursivePaths(recursivePaths);
}


void RemoteStore::ensurePath(const Path & path)
{
    openConnection();
    writeInt(wopEnsurePath, to);
    writeString(path, to);
    processStderr();
    readInt(from);
    /* Report path again so it can be added to chroot if new */
    reportRecursivePath(path);
}


void RemoteStore::addTempRoot(const Path & path)
{
    openConnection();
    writeInt(wopAddTempRoot, to);
    writeString(path, to);
    processStderr();
    readInt(from);
}


void RemoteStore::addIndirectRoot(const Path & path)
{
    openConnection();
    writeInt(wopAddIndirectRoot, to);
    writeString(path, to);
    processStderr();
    readInt(from);
}


void RemoteStore::syncWithGC()
{
    openConnection();
    writeInt(wopSyncWithGC, to);
    processStderr();
    readInt(from);
}


Roots RemoteStore::findRoots()
{
    openConnection();
    writeInt(wopFindRoots, to);
    processStderr();
    unsigned int count = readInt(from);
    Roots result;
    PathSet recursivePaths;
    while (count--) {
        Path link = readString(from);
        Path target = readStorePath(from);
        recursivePaths.insert(target);
        result[link] = target;
    }
    reportRecursivePaths(recursivePaths);
    return result;
}


void RemoteStore::collectGarbage(const GCOptions & options, GCResults & results)
{
    openConnection(false);

    writeInt(wopCollectGarbage, to);
    writeInt(options.action, to);
    writeStrings(options.pathsToDelete, to);
    writeInt(options.ignoreLiveness, to);
    writeLongLong(options.maxFreed, to);
    writeInt(0, to);
    if (GET_PROTOCOL_MINOR(daemonVersion) >= 5) {
        /* removed options */
        writeInt(0, to);
        writeInt(0, to);
    }

    processStderr();

    results.paths = readStrings<PathSet>(from);
    results.bytesFreed = readLongLong(from);
    readLongLong(from); // obsolete
}


PathSet RemoteStore::queryFailedPaths()
{
    openConnection();
    writeInt(wopQueryFailedPaths, to);
    processStderr();
    return readStorePaths<PathSet>(from);
}


void RemoteStore::clearFailedPaths(const PathSet & paths)
{
    openConnection();
    writeInt(wopClearFailedPaths, to);
    writeStrings(paths, to);
    processStderr();
    readInt(from);
}


static void writeRecursivePaths(int fd, const string & s) {
    lockFile(fd, ltWrite, true);
    writeFull(fd, (const unsigned char *) s.data(), s.size());
    unsigned char c;
    readFull(fd, &c, sizeof c);
    lockFile(fd, ltNone, true);
}


void RemoteStore::reportRecursivePath(const Path & path)
{
    if (fdRecursivePaths != -1) {
        if (path.size() >= 4096)
            throw Error("reporting a path name bigger than 4096 bytes not allowed");
        string s = path + '\0';
        writeRecursivePaths(fdRecursivePaths, s);
    }
}


void RemoteStore::reportRecursivePaths(const PathSet & paths)
{
    if (fdRecursivePaths != -1 && !paths.empty()) {
        string s = "";
        foreach (PathSet::const_iterator, i, paths) {
            if (s.size() + i->size() >= 4096) {
                if (i->size() >= 4096)
                    throw Error("reporting a path name bigger than 4096 bytes not allowed");
                writeRecursivePaths(fdRecursivePaths, s);
                s = "";
            }
            s += *i + '\0';
        }
        writeRecursivePaths(fdRecursivePaths, s);
    }
}


void RemoteStore::processStderr(Sink * sink, Source * source)
{
    to.flush();
    unsigned int msg;
    while ((msg = readInt(from)) == STDERR_NEXT
        || msg == STDERR_READ || msg == STDERR_WRITE) {
        if (msg == STDERR_WRITE) {
            string s = readString(from);
            if (!sink) throw Error("no sink");
            (*sink)((const unsigned char *) s.data(), s.size());
        }
        else if (msg == STDERR_READ) {
            if (!source) throw Error("no source");
            size_t len = readInt(from);
            unsigned char * buf = new unsigned char[len];
            AutoDeleteArray<unsigned char> d(buf);
            writeString(buf, source->read(buf, len), to);
            to.flush();
        }
        else {
            string s = readString(from);
            writeToStderr(s);
        }
    }
    if (msg == STDERR_ERROR) {
        string error = readString(from);
        unsigned int status = GET_PROTOCOL_MINOR(daemonVersion) >= 8 ? readInt(from) : 1;
        throw Error(format("%1%") % error, status);
    }
    else if (msg != STDERR_LAST)
        throw Error("protocol error processing standard error");
}


}
