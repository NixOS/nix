#include "serialise.hh"
#include "util.hh"
#include "remote-store.hh"
#include "worker-protocol.hh"
#include "archive.hh"
#include "affinity.hh"
#include "globals.hh"
#include "derivations.hh"
#include "pool.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>

namespace nix {


Path readStorePath(Store & store, Source & from)
{
    Path path = readString(from);
    store.assertStorePath(path);
    return path;
}


template<class T> T readStorePaths(Store & store, Source & from)
{
    T paths = readStrings<T>(from);
    for (auto & i : paths) store.assertStorePath(i);
    return paths;
}

template PathSet readStorePaths(Store & store, Source & from);


RemoteStore::RemoteStore(const Params & params, size_t maxConnections)
    : LocalFSStore(params)
    , connections(make_ref<Pool<Connection>>(
            maxConnections,
            [this]() { return openConnection(); },
            [](const ref<Connection> & r) { return r->to.good() && r->from.good(); }
            ))
{
}


std::string RemoteStore::getUri()
{
    return "daemon";
}


ref<RemoteStore::Connection> RemoteStore::openConnection()
{
    auto conn = make_ref<Connection>();

    /* Connect to a daemon that does the privileged work for us. */
    conn->fd = socket(PF_UNIX, SOCK_STREAM
        #ifdef SOCK_CLOEXEC
        | SOCK_CLOEXEC
        #endif
        , 0);
    if (!conn->fd)
        throw SysError("cannot create Unix domain socket");
    closeOnExec(conn->fd.get());

    string socketPath = settings.nixDaemonSocketFile;

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    if (socketPath.size() + 1 >= sizeof(addr.sun_path))
        throw Error(format("socket path ‘%1%’ is too long") % socketPath);
    strcpy(addr.sun_path, socketPath.c_str());

    if (connect(conn->fd.get(), (struct sockaddr *) &addr, sizeof(addr)) == -1)
        throw SysError(format("cannot connect to daemon at ‘%1%’") % socketPath);

    conn->from.fd = conn->fd.get();
    conn->to.fd = conn->fd.get();

    /* Send the magic greeting, check for the reply. */
    try {
        conn->to << WORKER_MAGIC_1;
        conn->to.flush();
        unsigned int magic = readInt(conn->from);
        if (magic != WORKER_MAGIC_2) throw Error("protocol mismatch");

        conn->daemonVersion = readInt(conn->from);
        if (GET_PROTOCOL_MAJOR(conn->daemonVersion) != GET_PROTOCOL_MAJOR(PROTOCOL_VERSION))
            throw Error("Nix daemon protocol version not supported");
        if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 10)
            throw Error("the Nix daemon version is too old");
        conn->to << PROTOCOL_VERSION;

        if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 14) {
            int cpu = settings.lockCPU ? lockToCurrentCPU() : -1;
            if (cpu != -1)
                conn->to << 1 << cpu;
            else
                conn->to << 0;
        }

        if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 11)
            conn->to << false;

        conn->processStderr();
    }
    catch (Error & e) {
        throw Error(format("cannot start daemon worker: %1%") % e.msg());
    }

    setOptions(conn);

    return conn;
}


void RemoteStore::setOptions(ref<Connection> conn)
{
    conn->to << wopSetOptions
       << settings.keepFailed
       << settings.keepGoing
       << settings.tryFallback
       << verbosity
       << settings.maxBuildJobs
       << settings.maxSilentTime
       << settings.useBuildHook
       << (settings.verboseBuild ? lvlError : lvlVomit)
       << 0 // obsolete log type
       << 0 /* obsolete print build trace */
       << settings.buildCores
       << settings.useSubstitutes;

    if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 12) {
        Settings::SettingsMap overrides = settings.getOverrides();
        if (overrides["ssh-auth-sock"] == "")
            overrides["ssh-auth-sock"] = getEnv("SSH_AUTH_SOCK");
        conn->to << overrides.size();
        for (auto & i : overrides)
            conn->to << i.first << i.second;
    }

    conn->processStderr();
}


bool RemoteStore::isValidPathUncached(const Path & path)
{
    auto conn(connections->get());
    conn->to << wopIsValidPath << path;
    conn->processStderr();
    unsigned int reply = readInt(conn->from);
    return reply != 0;
}


PathSet RemoteStore::queryValidPaths(const PathSet & paths)
{
    auto conn(connections->get());
    if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 12) {
        PathSet res;
        for (auto & i : paths)
            if (isValidPath(i)) res.insert(i);
        return res;
    } else {
        conn->to << wopQueryValidPaths << paths;
        conn->processStderr();
        return readStorePaths<PathSet>(*this, conn->from);
    }
}


PathSet RemoteStore::queryAllValidPaths()
{
    auto conn(connections->get());
    conn->to << wopQueryAllValidPaths;
    conn->processStderr();
    return readStorePaths<PathSet>(*this, conn->from);
}


PathSet RemoteStore::querySubstitutablePaths(const PathSet & paths)
{
    auto conn(connections->get());
    if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 12) {
        PathSet res;
        for (auto & i : paths) {
            conn->to << wopHasSubstitutes << i;
            conn->processStderr();
            if (readInt(conn->from)) res.insert(i);
        }
        return res;
    } else {
        conn->to << wopQuerySubstitutablePaths << paths;
        conn->processStderr();
        return readStorePaths<PathSet>(*this, conn->from);
    }
}


void RemoteStore::querySubstitutablePathInfos(const PathSet & paths,
    SubstitutablePathInfos & infos)
{
    if (paths.empty()) return;

    auto conn(connections->get());

    if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 12) {

        for (auto & i : paths) {
            SubstitutablePathInfo info;
            conn->to << wopQuerySubstitutablePathInfo << i;
            conn->processStderr();
            unsigned int reply = readInt(conn->from);
            if (reply == 0) continue;
            info.deriver = readString(conn->from);
            if (info.deriver != "") assertStorePath(info.deriver);
            info.references = readStorePaths<PathSet>(*this, conn->from);
            info.downloadSize = readLongLong(conn->from);
            info.narSize = readLongLong(conn->from);
            infos[i] = info;
        }

    } else {

        conn->to << wopQuerySubstitutablePathInfos << paths;
        conn->processStderr();
        unsigned int count = readInt(conn->from);
        for (unsigned int n = 0; n < count; n++) {
            Path path = readStorePath(*this, conn->from);
            SubstitutablePathInfo & info(infos[path]);
            info.deriver = readString(conn->from);
            if (info.deriver != "") assertStorePath(info.deriver);
            info.references = readStorePaths<PathSet>(*this, conn->from);
            info.downloadSize = readLongLong(conn->from);
            info.narSize = readLongLong(conn->from);
        }

    }
}


std::shared_ptr<ValidPathInfo> RemoteStore::queryPathInfoUncached(const Path & path)
{
    auto conn(connections->get());
    conn->to << wopQueryPathInfo << path;
    try {
        conn->processStderr();
    } catch (Error & e) {
        // Ugly backwards compatibility hack.
        if (e.msg().find("is not valid") != std::string::npos)
            throw InvalidPath(e.what());
        throw;
    }
    if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 17) {
        bool valid = readInt(conn->from) != 0;
        if (!valid) throw InvalidPath(format("path ‘%s’ is not valid") % path);
    }
    auto info = std::make_shared<ValidPathInfo>();
    info->path = path;
    info->deriver = readString(conn->from);
    if (info->deriver != "") assertStorePath(info->deriver);
    info->narHash = parseHash(htSHA256, readString(conn->from));
    info->references = readStorePaths<PathSet>(*this, conn->from);
    info->registrationTime = readInt(conn->from);
    info->narSize = readLongLong(conn->from);
    if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 16) {
        info->ultimate = readInt(conn->from) != 0;
        info->sigs = readStrings<StringSet>(conn->from);
        info->ca = readString(conn->from);
    }
    return info;
}


void RemoteStore::queryReferrers(const Path & path,
    PathSet & referrers)
{
    auto conn(connections->get());
    conn->to << wopQueryReferrers << path;
    conn->processStderr();
    PathSet referrers2 = readStorePaths<PathSet>(*this, conn->from);
    referrers.insert(referrers2.begin(), referrers2.end());
}


PathSet RemoteStore::queryValidDerivers(const Path & path)
{
    auto conn(connections->get());
    conn->to << wopQueryValidDerivers << path;
    conn->processStderr();
    return readStorePaths<PathSet>(*this, conn->from);
}


PathSet RemoteStore::queryDerivationOutputs(const Path & path)
{
    auto conn(connections->get());
    conn->to << wopQueryDerivationOutputs << path;
    conn->processStderr();
    return readStorePaths<PathSet>(*this, conn->from);
}


PathSet RemoteStore::queryDerivationOutputNames(const Path & path)
{
    auto conn(connections->get());
    conn->to << wopQueryDerivationOutputNames << path;
    conn->processStderr();
    return readStrings<PathSet>(conn->from);
}


Path RemoteStore::queryPathFromHashPart(const string & hashPart)
{
    auto conn(connections->get());
    conn->to << wopQueryPathFromHashPart << hashPart;
    conn->processStderr();
    Path path = readString(conn->from);
    if (!path.empty()) assertStorePath(path);
    return path;
}


void RemoteStore::addToStore(const ValidPathInfo & info, const std::string & nar,
    bool repair, bool dontCheckSigs)
{
    throw Error("RemoteStore::addToStore() not implemented");
}


Path RemoteStore::addToStore(const string & name, const Path & _srcPath,
    bool recursive, HashType hashAlgo, PathFilter & filter, bool repair)
{
    if (repair) throw Error("repairing is not supported when building through the Nix daemon");

    auto conn(connections->get());

    Path srcPath(absPath(_srcPath));

    conn->to << wopAddToStore << name
       << ((hashAlgo == htSHA256 && recursive) ? 0 : 1) /* backwards compatibility hack */
       << (recursive ? 1 : 0)
       << printHashType(hashAlgo);

    try {
        conn->to.written = 0;
        conn->to.warn = true;
        dumpPath(srcPath, conn->to, filter);
        conn->to.warn = false;
        conn->processStderr();
    } catch (SysError & e) {
        /* Daemon closed while we were sending the path. Probably OOM
           or I/O error. */
        if (e.errNo == EPIPE)
            try {
                conn->processStderr();
            } catch (EndOfFile & e) { }
        throw;
    }

    return readStorePath(*this, conn->from);
}


Path RemoteStore::addTextToStore(const string & name, const string & s,
    const PathSet & references, bool repair)
{
    if (repair) throw Error("repairing is not supported when building through the Nix daemon");

    auto conn(connections->get());
    conn->to << wopAddTextToStore << name << s << references;

    conn->processStderr();
    return readStorePath(*this, conn->from);
}


void RemoteStore::buildPaths(const PathSet & drvPaths, BuildMode buildMode)
{
    auto conn(connections->get());
    conn->to << wopBuildPaths;
    if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 13) {
        conn->to << drvPaths;
        if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 15)
            conn->to << buildMode;
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
        conn->to << drvPaths2;
    }
    conn->processStderr();
    readInt(conn->from);
}


BuildResult RemoteStore::buildDerivation(const Path & drvPath, const BasicDerivation & drv,
    BuildMode buildMode)
{
    auto conn(connections->get());
    conn->to << wopBuildDerivation << drvPath << drv << buildMode;
    conn->processStderr();
    BuildResult res;
    unsigned int status;
    conn->from >> status >> res.errorMsg;
    res.status = (BuildResult::Status) status;
    return res;
}


void RemoteStore::ensurePath(const Path & path)
{
    auto conn(connections->get());
    conn->to << wopEnsurePath << path;
    conn->processStderr();
    readInt(conn->from);
}


void RemoteStore::addTempRoot(const Path & path)
{
    auto conn(connections->get());
    conn->to << wopAddTempRoot << path;
    conn->processStderr();
    readInt(conn->from);
}


void RemoteStore::addIndirectRoot(const Path & path)
{
    auto conn(connections->get());
    conn->to << wopAddIndirectRoot << path;
    conn->processStderr();
    readInt(conn->from);
}


void RemoteStore::syncWithGC()
{
    auto conn(connections->get());
    conn->to << wopSyncWithGC;
    conn->processStderr();
    readInt(conn->from);
}


Roots RemoteStore::findRoots()
{
    auto conn(connections->get());
    conn->to << wopFindRoots;
    conn->processStderr();
    unsigned int count = readInt(conn->from);
    Roots result;
    while (count--) {
        Path link = readString(conn->from);
        Path target = readStorePath(*this, conn->from);
        result[link] = target;
    }
    return result;
}


void RemoteStore::collectGarbage(const GCOptions & options, GCResults & results)
{
    auto conn(connections->get());

    conn->to
        << wopCollectGarbage << options.action << options.pathsToDelete << options.ignoreLiveness
        << options.maxFreed
        /* removed options */
        << 0 << 0 << 0;

    conn->processStderr();

    results.paths = readStrings<PathSet>(conn->from);
    results.bytesFreed = readLongLong(conn->from);
    readLongLong(conn->from); // obsolete

    {
        auto state_(Store::state.lock());
        state_->pathInfoCache.clear();
    }
}


void RemoteStore::optimiseStore()
{
    auto conn(connections->get());
    conn->to << wopOptimiseStore;
    conn->processStderr();
    readInt(conn->from);
}


bool RemoteStore::verifyStore(bool checkContents, bool repair)
{
    auto conn(connections->get());
    conn->to << wopVerifyStore << checkContents << repair;
    conn->processStderr();
    return readInt(conn->from) != 0;
}


void RemoteStore::addSignatures(const Path & storePath, const StringSet & sigs)
{
    auto conn(connections->get());
    conn->to << wopAddSignatures << storePath << sigs;
    conn->processStderr();
    readInt(conn->from);
}


RemoteStore::Connection::~Connection()
{
    try {
        to.flush();
        fd = -1;
    } catch (...) {
        ignoreException();
    }
}


void RemoteStore::Connection::processStderr(Sink * sink, Source * source)
{
    to.flush();
    unsigned int msg;
    while ((msg = readInt(from)) == STDERR_NEXT
        || msg == STDERR_READ || msg == STDERR_WRITE) {
        if (msg == STDERR_WRITE) {
            string s = readString(from);
            if (!sink) throw Error("no sink");
            (*sink)(s);
        }
        else if (msg == STDERR_READ) {
            if (!source) throw Error("no source");
            size_t len = readInt(from);
            unsigned char * buf = new unsigned char[len];
            AutoDeleteArray<unsigned char> d(buf);
            writeString(buf, source->read(buf, len), to);
            to.flush();
        }
        else
            printMsg(lvlError, chomp(readString(from)));
    }
    if (msg == STDERR_ERROR) {
        string error = readString(from);
        unsigned int status = readInt(from);
        throw Error(format("%1%") % error, status);
    }
    else if (msg != STDERR_LAST)
        throw Error("protocol error processing standard error");
}


}
