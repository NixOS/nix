#include "serialise.hh"
#include "util.hh"
#include "remote-store.hh"
#include "worker-protocol.hh"
#include "archive.hh"
#include "affinity.hh"
#include "globals.hh"
#include "derivations.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
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
    for (auto & i : paths) assertStorePath(i);
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
    else
        throw Error(format("invalid setting for NIX_REMOTE, ‘%1%’") % remoteMode);

    from.fd = fdSocket;
    to.fd = fdSocket;

    /* Send the magic greeting, check for the reply. */
    try {
        to << WORKER_MAGIC_1;
        to.flush();
        unsigned int magic = readInt(from);
        if (magic != WORKER_MAGIC_2) throw Error("protocol mismatch");

        daemonVersion = readInt(from);
        if (GET_PROTOCOL_MAJOR(daemonVersion) != GET_PROTOCOL_MAJOR(PROTOCOL_VERSION))
            throw Error("Nix daemon protocol version not supported");
        to << PROTOCOL_VERSION;

        if (GET_PROTOCOL_MINOR(daemonVersion) >= 14) {
            int cpu = settings.lockCPU ? lockToCurrentCPU() : -1;
            if (cpu != -1)
                to << 1 << cpu;
            else
                to << 0;
        }

        if (GET_PROTOCOL_MINOR(daemonVersion) >= 11)
            to << reserveSpace;

        processStderr();
    }
    catch (Error & e) {
        throw Error(format("cannot start daemon worker: %1%") % e.msg());
    }

    setOptions();
}


void RemoteStore::connectToDaemon()
{
    fdSocket = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fdSocket == -1)
        throw SysError("cannot create Unix domain socket");
    closeOnExec(fdSocket);

    string socketPath = settings.nixDaemonSocketFile;

    /* Urgh, sockaddr_un allows path names of only 108 characters.  So
       chdir to the socket directory so that we can pass a relative
       path name.  !!! this is probably a bad idea in multi-threaded
       applications... */
    AutoCloseFD fdPrevDir = open(".", O_RDONLY);
    if (fdPrevDir == -1) throw SysError("couldn't open current directory");
    if (chdir(dirOf(socketPath).c_str()) == -1) throw SysError(format("couldn't change to directory of ‘%1%’") % socketPath);
    Path socketPathRel = "./" + baseNameOf(socketPath);

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    if (socketPathRel.size() >= sizeof(addr.sun_path))
        throw Error(format("socket path ‘%1%’ is too long") % socketPathRel);
    using namespace std;
    strcpy(addr.sun_path, socketPathRel.c_str());

    if (connect(fdSocket, (struct sockaddr *) &addr, sizeof(addr)) == -1)
        throw SysError(format("cannot connect to daemon at ‘%1%’") % socketPath);

    if (fchdir(fdPrevDir) == -1)
        throw SysError("couldn't change back to previous directory");
}


RemoteStore::~RemoteStore()
{
    try {
        to.flush();
        fdSocket.close();
    } catch (...) {
        ignoreException();
    }
}


void RemoteStore::setOptions()
{
    to << wopSetOptions
       << settings.keepFailed
       << settings.keepGoing
       << settings.tryFallback
       << verbosity
       << settings.maxBuildJobs
       << settings.maxSilentTime;
    if (GET_PROTOCOL_MINOR(daemonVersion) >= 2)
        to << settings.useBuildHook;
    if (GET_PROTOCOL_MINOR(daemonVersion) >= 4)
        to << settings.buildVerbosity
           << logType
           << settings.printBuildTrace;
    if (GET_PROTOCOL_MINOR(daemonVersion) >= 6)
        to << settings.buildCores;
    if (GET_PROTOCOL_MINOR(daemonVersion) >= 10)
        to << settings.useSubstitutes;

    if (GET_PROTOCOL_MINOR(daemonVersion) >= 12) {
        Settings::SettingsMap overrides = settings.getOverrides();
        if (overrides["ssh-auth-sock"] == "")
            overrides["ssh-auth-sock"] = getEnv("SSH_AUTH_SOCK");
        to << overrides.size();
        for (auto & i : overrides)
            to << i.first << i.second;
    }

    processStderr();
}


bool RemoteStore::isValidPath(const Path & path)
{
    openConnection();
    to << wopIsValidPath << path;
    processStderr();
    unsigned int reply = readInt(from);
    return reply != 0;
}


PathSet RemoteStore::queryValidPaths(const PathSet & paths)
{
    openConnection();
    if (GET_PROTOCOL_MINOR(daemonVersion) < 12) {
        PathSet res;
        for (auto & i : paths)
            if (isValidPath(i)) res.insert(i);
        return res;
    } else {
        to << wopQueryValidPaths << paths;
        processStderr();
        return readStorePaths<PathSet>(from);
    }
}


PathSet RemoteStore::queryAllValidPaths()
{
    openConnection();
    to << wopQueryAllValidPaths;
    processStderr();
    return readStorePaths<PathSet>(from);
}


PathSet RemoteStore::querySubstitutablePaths(const PathSet & paths)
{
    openConnection();
    if (GET_PROTOCOL_MINOR(daemonVersion) < 12) {
        PathSet res;
        for (auto & i : paths) {
            to << wopHasSubstitutes << i;
            processStderr();
            if (readInt(from)) res.insert(i);
        }
        return res;
    } else {
        to << wopQuerySubstitutablePaths << paths;
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

        for (auto & i : paths) {
            SubstitutablePathInfo info;
            to << wopQuerySubstitutablePathInfo << i;
            processStderr();
            unsigned int reply = readInt(from);
            if (reply == 0) continue;
            info.deriver = readString(from);
            if (info.deriver != "") assertStorePath(info.deriver);
            info.references = readStorePaths<PathSet>(from);
            info.downloadSize = readLongLong(from);
            info.narSize = GET_PROTOCOL_MINOR(daemonVersion) >= 7 ? readLongLong(from) : 0;
            infos[i] = info;
        }

    } else {

        to << wopQuerySubstitutablePathInfos << paths;
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


ValidPathInfo RemoteStore::queryPathInfo(const Path & path)
{
    openConnection();
    to << wopQueryPathInfo << path;
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
    to << wopQueryPathHash << path;
    processStderr();
    string hash = readString(from);
    return parseHash(htSHA256, hash);
}


void RemoteStore::queryReferences(const Path & path,
    PathSet & references)
{
    openConnection();
    to << wopQueryReferences << path;
    processStderr();
    PathSet references2 = readStorePaths<PathSet>(from);
    references.insert(references2.begin(), references2.end());
}


void RemoteStore::queryReferrers(const Path & path,
    PathSet & referrers)
{
    openConnection();
    to << wopQueryReferrers << path;
    processStderr();
    PathSet referrers2 = readStorePaths<PathSet>(from);
    referrers.insert(referrers2.begin(), referrers2.end());
}


Path RemoteStore::queryDeriver(const Path & path)
{
    openConnection();
    to << wopQueryDeriver << path;
    processStderr();
    Path drvPath = readString(from);
    if (drvPath != "") assertStorePath(drvPath);
    return drvPath;
}


PathSet RemoteStore::queryValidDerivers(const Path & path)
{
    openConnection();
    to << wopQueryValidDerivers << path;
    processStderr();
    return readStorePaths<PathSet>(from);
}


PathSet RemoteStore::queryDerivationOutputs(const Path & path)
{
    openConnection();
    to << wopQueryDerivationOutputs << path;
    processStderr();
    return readStorePaths<PathSet>(from);
}


PathSet RemoteStore::queryDerivationOutputNames(const Path & path)
{
    openConnection();
    to << wopQueryDerivationOutputNames << path;
    processStderr();
    return readStrings<PathSet>(from);
}


Path RemoteStore::queryPathFromHashPart(const string & hashPart)
{
    openConnection();
    to << wopQueryPathFromHashPart << hashPart;
    processStderr();
    Path path = readString(from);
    if (!path.empty()) assertStorePath(path);
    return path;
}


Path RemoteStore::addToStore(const string & name, const Path & _srcPath,
    bool recursive, HashType hashAlgo, PathFilter & filter, bool repair)
{
    if (repair) throw Error("repairing is not supported when building through the Nix daemon");

    openConnection();

    Path srcPath(absPath(_srcPath));

    to << wopAddToStore << name
       << ((hashAlgo == htSHA256 && recursive) ? 0 : 1) /* backwards compatibility hack */
       << (recursive ? 1 : 0)
       << printHashType(hashAlgo);

    try {
        to.written = 0;
        to.warn = true;
        dumpPath(srcPath, to, filter);
        to.warn = false;
        processStderr();
    } catch (SysError & e) {
        /* Daemon closed while we were sending the path. Probably OOM
           or I/O error. */
        if (e.errNo == EPIPE)
            try {
                processStderr();
            } catch (EndOfFile & e) { }
        throw;
    }

    return readStorePath(from);
}


Path RemoteStore::addTextToStore(const string & name, const string & s,
    const PathSet & references, bool repair)
{
    if (repair) throw Error("repairing is not supported when building through the Nix daemon");

    openConnection();
    to << wopAddTextToStore << name << s << references;

    processStderr();
    return readStorePath(from);
}


void RemoteStore::exportPath(const Path & path, bool sign,
    Sink & sink)
{
    openConnection();
    to << wopExportPath << path << (sign ? 1 : 0);
    processStderr(&sink); /* sink receives the actual data */
    readInt(from);
}


Paths RemoteStore::importPaths(bool requireSignature, Source & source)
{
    openConnection();
    to << wopImportPaths;
    /* We ignore requireSignature, since the worker forces it to true
       anyway. */
    processStderr(0, &source);
    return readStorePaths<Paths>(from);
}


void RemoteStore::buildPaths(const PathSet & drvPaths, BuildMode buildMode)
{
    openConnection();
    to << wopBuildPaths;
    if (GET_PROTOCOL_MINOR(daemonVersion) >= 13) {
        to << drvPaths;
        if (GET_PROTOCOL_MINOR(daemonVersion) >= 15)
            to << buildMode;
        else
            /* Old daemons did not take a 'buildMode' parameter, so we
               need to validate it here on the client side.  */
            if (buildMode != bmNormal)
                throw Error("repairing or checking is not supported when building through the Nix daemon");
    } else {
        /* For backwards compatibility with old daemons, strip output
           identifiers. */
        PathSet drvPaths2;
        for (auto & i : drvPaths)
            drvPaths2.insert(string(i, 0, i.find('!')));
        to << drvPaths2;
    }
    processStderr();
    readInt(from);
}


BuildResult RemoteStore::buildDerivation(const Path & drvPath, const BasicDerivation & drv,
    BuildMode buildMode)
{
    openConnection();
    to << wopBuildDerivation << drvPath << drv << buildMode;
    processStderr();
    BuildResult res;
    unsigned int status;
    from >> status >> res.errorMsg;
    res.status = (BuildResult::Status) status;
    return res;
}


void RemoteStore::ensurePath(const Path & path)
{
    openConnection();
    to << wopEnsurePath << path;
    processStderr();
    readInt(from);
}


void RemoteStore::addTempRoot(const Path & path)
{
    openConnection();
    to << wopAddTempRoot << path;
    processStderr();
    readInt(from);
}


void RemoteStore::addIndirectRoot(const Path & path)
{
    openConnection();
    to << wopAddIndirectRoot << path;
    processStderr();
    readInt(from);
}


void RemoteStore::syncWithGC()
{
    openConnection();
    to << wopSyncWithGC;
    processStderr();
    readInt(from);
}


Roots RemoteStore::findRoots()
{
    openConnection();
    to << wopFindRoots;
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

    to << wopCollectGarbage << options.action << options.pathsToDelete << options.ignoreLiveness
       << options.maxFreed << 0;
    if (GET_PROTOCOL_MINOR(daemonVersion) >= 5)
        /* removed options */
        to << 0 << 0;

    processStderr();

    results.paths = readStrings<PathSet>(from);
    results.bytesFreed = readLongLong(from);
    readLongLong(from); // obsolete
}


PathSet RemoteStore::queryFailedPaths()
{
    openConnection();
    to << wopQueryFailedPaths;
    processStderr();
    return readStorePaths<PathSet>(from);
}


void RemoteStore::clearFailedPaths(const PathSet & paths)
{
    openConnection();
    to << wopClearFailedPaths << paths;
    processStderr();
    readInt(from);
}

void RemoteStore::optimiseStore()
{
    openConnection();
    to << wopOptimiseStore;
    processStderr();
    readInt(from);
}

bool RemoteStore::verifyStore(bool checkContents, bool repair)
{
    openConnection();
    to << wopVerifyStore << checkContents << repair;
    processStderr();
    return readInt(from) != 0;
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
