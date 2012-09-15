#include "serialise.hh"
#include "util.hh"
#include "remote-store.hh"
#include "worker-protocol.hh"
#include "archive.hh"
#include "globals.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>

#include <iostream>
#include <unistd.h>
#include <cstring>
#include <errno.h>

namespace nix {


Path readStorePath(Source & from)
{
    Path path = readString(from);
    assertStorePath(path);
    return path;
}


static int readFD(FdSource & from)
{
    struct msghdr msg = {0};
    struct cmsghdr *cmsg;
    struct iovec iov;
    int res;
    unsigned char control[CMSG_SPACE(sizeof res)];

    char buf;
    iov.iov_base = &buf;
    iov.iov_len = sizeof buf;

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof control;

    errno = 0;
    ssize_t ret = recvmsg(from.fd, &msg, 0);
    if (ret < sizeof buf ||
        msg.msg_flags & MSG_EOR ||
        msg.msg_flags & MSG_OOB ||
        msg.msg_flags & MSG_TRUNC ||
        msg.msg_flags & MSG_CTRUNC) {
        if (errno == EINTR) return readFD(from);
        throw SysError("reading fd from socket");
    }
    cmsg = CMSG_FIRSTHDR(&msg);
    memcpy(&res, CMSG_DATA(cmsg), sizeof res);
    return res;
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

    if (remoteMode == "slave")
        /* Fork off a setuid worker to do the privileged work. */
        forkSlave();
    else if (remoteMode == "daemon")
        /* Connect to a daemon that does the privileged work for
           us. */
       connectToDaemon();
    else
         throw Error(format("invalid setting for NIX_REMOTE, `%1%'")
             % remoteMode);

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


void RemoteStore::forkSlave()
{
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == -1)
        throw SysError("cannot create sockets");

    fdSocket = sockets[0];
    AutoCloseFD fdChild = sockets[1];

    /* Start the worker. */
    Path worker = getEnv("NIX_WORKER");
    if (worker == "")
        worker = settings.nixBinDir + "/nix-worker";

    child = fork();

    switch (child) {

    case -1:
        throw SysError("unable to fork");

    case 0:
        try { /* child */

            if (dup2(fdChild, STDOUT_FILENO) == -1)
                throw SysError("dupping write side");

            if (dup2(fdChild, STDIN_FILENO) == -1)
                throw SysError("dupping read side");

            close(fdSocket);
            close(fdChild);

            execlp(worker.c_str(), worker.c_str(), "--slave", NULL);

            throw SysError(format("executing `%1%'") % worker);

        } catch (std::exception & e) {
            std::cerr << format("child error: %1%\n") % e.what();
        }
        quickExit(1);
    }

    fdChild.close();

}


void RemoteStore::connectToDaemon()
{
    fdSocket = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fdSocket == -1)
        throw SysError("cannot create Unix domain socket");

    string socketPath = settings.nixStateDir + DEFAULT_SOCKET_PATH;

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
    return readStorePaths<PathSet>(from);
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

    if (GET_PROTOCOL_MINOR(daemonVersion) < 12) {

        foreach (PathSet::const_iterator, i, paths) {
            SubstitutablePathInfo info;
            writeInt(wopQuerySubstitutablePathInfo, to);
            writeString(*i, to);
            processStderr();
            unsigned int reply = readInt(from);
            if (reply == 0) continue;
            info.deriver = readString(from);
            if (info.deriver != "") assertStorePath(info.deriver);
            info.references = readStorePaths<PathSet>(from);
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
            if (info.deriver != "") assertStorePath(info.deriver);
            info.references = readStorePaths<PathSet>(from);
            info.downloadSize = readLongLong(from);
            info.narSize = readLongLong(from);
        }

    }
}


PathSet RemoteStore::querySubstitutableFiles(const PathSet & paths)
{
    openConnection();

    if (GET_PROTOCOL_MINOR(daemonVersion) < 13) return PathSet();

    writeInt(wopQuerySubstitutableFiles, to);
    writeStrings(paths, to);
    processStderr();
    return readStrings<PathSet>(from);
}


void RemoteStore::querySubstitutableFileInfos(const PathSet & paths,
    SubstitutableFileInfos & infos)
{
    if (paths.empty()) return;

    openConnection();

    if (GET_PROTOCOL_MINOR(daemonVersion) < 13) return;

    writeInt(wopQuerySubstitutableFileInfos, to);
    writeStrings(paths, to);
    processStderr();
    unsigned int count = readInt(from);
    for (unsigned int n = 0; n < count; n++) {
        Path path = readString(from);
        SubstitutableFileInfo & info(infos[path]);
        info.substituter = readString(from);
        string type = readString(from);
        if (type == "regular") {
            info.type = tpRegular;
            info.regular.executable = readInt(from) != 0;
            info.regular.length = readLongLong(from);
            info.regular.hash = parseHash32(htSHA256, readString(from));
        } else if (type == "symlink") {
            info.type = tpSymlink;
            info.target = readString(from);
        } else if (type == "directory") {
            info.type = tpDirectory;
            int nrEntries = readInt(from);
            while (nrEntries--)
                info.files.insert(readString(from));
        } else
            info.type = tpUnknown;
    }
}


void RemoteStore::readSubstitutableFile(const Path & path, const SubstitutableFileInfo & info, FdPair & fds) {
    openConnection();

    assert(GET_PROTOCOL_MINOR(daemonVersion) >= 13);

    writeInt(wopReadSubstitutableFile, to);
    writeString(path, to);
    writeString(info.substituter, to);
    switch (info.type)
    {
        case tpRegular:
            writeString("regular", to);
            writeInt(info.regular.executable, to);
            writeLongLong(info.regular.length, to);
            writeString(printHash32(info.regular.hash), to);
            break;
        case tpSymlink:
            writeString("symlink", to);
            writeString(info.target, to);
            break;
        case tpDirectory:
            writeString("directory", to);
            writeStrings(info.files, to);
            break;
        case tpUnknown:
            writeString("unknown", to);
            break;
    }
    processStderr();
    /* Needed to ensure delay of writing fd until after stderr is processed
       readUnbuffered reads the full bufSize, which could read the dummy byte
       that accompanies the written fd, discarding it. */
    writeInt(0,to);
    to.flush();
    fds.first = readFD(from);
    fds.second = readFD(from);
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
    if (info.deriver != "") assertStorePath(info.deriver);
    info.hash = parseHash(htSHA256, readString(from));
    info.references = readStorePaths<PathSet>(from);
    info.registrationTime = readInt(from);
    info.narSize = readLongLong(from);
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
    referrers.insert(referrers2.begin(), referrers2.end());
}


Path RemoteStore::queryDeriver(const Path & path)
{
    openConnection();
    writeInt(wopQueryDeriver, to);
    writeString(path, to);
    processStderr();
    Path drvPath = readString(from);
    if (drvPath != "") assertStorePath(drvPath);
    return drvPath;
}


PathSet RemoteStore::queryDerivationOutputs(const Path & path)
{
    openConnection();
    writeInt(wopQueryDerivationOutputs, to);
    writeString(path, to);
    processStderr();
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
    if (!path.empty()) assertStorePath(path);
    return path;
}


Path RemoteStore::addToStore(const Path & _srcPath,
    bool recursive, HashType hashAlgo, PathFilter & filter)
{
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
    return readStorePath(from);
}


Path RemoteStore::addTextToStore(const string & name, const string & s,
    const PathSet & references)
{
    openConnection();
    writeInt(wopAddTextToStore, to);
    writeString(name, to);
    writeString(s, to);
    writeStrings(references, to);

    processStderr();
    return readStorePath(from);
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
    return readStorePaths<Paths>(from);
}


void RemoteStore::buildPaths(const PathSet & drvPaths)
{
    openConnection();
    writeInt(wopBuildPaths, to);
    writeStrings(drvPaths, to);
    processStderr();
    readInt(from);
}


void RemoteStore::ensurePath(const Path & path)
{
    openConnection();
    writeInt(wopEnsurePath, to);
    writeString(path, to);
    processStderr();
    readInt(from);
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
    while (count--) {
        Path link = readString(from);
        Path target = readStorePath(from);
        result[link] = target;
    }
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
            writeToStderr((const unsigned char *) s.data(), s.size());
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
