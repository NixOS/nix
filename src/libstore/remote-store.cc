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


namespace nix {


Path readStorePath(Source & from)
{
    Path path = readString(from);
    assertStorePath(path);
    return path;
}


PathSet readStorePaths(Source & from)
{
    PathSet paths = readStringSet(from);
    for (PathSet::iterator i = paths.begin(); i != paths.end(); ++i)
        assertStorePath(*i);
    return paths;
}


RemoteStore::RemoteStore()
{
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
        writeInt(verbosity, to);
        unsigned int magic = readInt(from);
        if (magic != WORKER_MAGIC_2) throw Error("protocol mismatch");
        processStderr();
    } catch (Error & e) {
        throw Error(format("cannot start worker (%1%)")
            % e.msg());
    }
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
        worker = nixBinDir + "/nix-worker";

    string verbosityArg = "-";
    for (int i = 1; i < verbosity; ++i)
        verbosityArg += "v";

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

            execlp(worker.c_str(), worker.c_str(), "--slave",
                /* hacky - must be at the end */
                verbosityArg == "-" ? NULL : verbosityArg.c_str(),
                NULL);

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

    string socketPath = nixStateDir + DEFAULT_SOCKET_PATH;

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    if (socketPath.size() >= sizeof(addr.sun_path))
        throw Error(format("socket path `%1%' is too long") % socketPath);
    strcpy(addr.sun_path, socketPath.c_str());
    
    if (connect(fdSocket, (struct sockaddr *) &addr, sizeof(addr)) == -1)
        throw SysError(format("cannot connect to daemon at `%1%'") % socketPath);
}


RemoteStore::~RemoteStore()
{
    try {
        fdSocket.close();
        if (child != -1)
            child.wait(true);
    } catch (Error & e) {
        printMsg(lvlError, format("error (ignored): %1%") % e.msg());
    }
}


bool RemoteStore::isValidPath(const Path & path)
{
    writeInt(wopIsValidPath, to);
    writeString(path, to);
    processStderr();
    unsigned int reply = readInt(from);
    return reply != 0;
}


Substitutes RemoteStore::querySubstitutes(const Path & path)
{
    throw Error("not implemented 2");
}


bool RemoteStore::hasSubstitutes(const Path & path)
{
    writeInt(wopHasSubstitutes, to);
    writeString(path, to);
    processStderr();
    unsigned int reply = readInt(from);
    return reply != 0;
}


Hash RemoteStore::queryPathHash(const Path & path)
{
    writeInt(wopQueryPathHash, to);
    writeString(path, to);
    processStderr();
    string hash = readString(from);
    return parseHash(htSHA256, hash);
}


void RemoteStore::queryReferences(const Path & path,
    PathSet & references)
{
    writeInt(wopQueryReferences, to);
    writeString(path, to);
    processStderr();
    PathSet references2 = readStorePaths(from);
    references.insert(references2.begin(), references2.end());
}


void RemoteStore::queryReferrers(const Path & path,
    PathSet & referrers)
{
    writeInt(wopQueryReferrers, to);
    writeString(path, to);
    processStderr();
    PathSet referrers2 = readStorePaths(from);
    referrers.insert(referrers2.begin(), referrers2.end());
}


Path RemoteStore::addToStore(const Path & _srcPath, bool fixed,
    bool recursive, string hashAlgo, PathFilter & filter)
{
    Path srcPath(absPath(_srcPath));
    
    writeInt(wopAddToStore, to);
    writeString(baseNameOf(srcPath), to);
    writeInt(fixed ? 1 : 0, to);
    writeInt(recursive ? 1 : 0, to);
    writeString(hashAlgo, to);
    dumpPath(srcPath, to, filter);
    processStderr();
    Path path = readStorePath(from);
    return path;
}


Path RemoteStore::addTextToStore(const string & suffix, const string & s,
    const PathSet & references)
{
    writeInt(wopAddTextToStore, to);
    writeString(suffix, to);
    writeString(s, to);
    writeStringSet(references, to);
    
    processStderr();
    Path path = readStorePath(from);
    return path;
}


void RemoteStore::exportPath(const Path & path, bool sign,
    Sink & sink)
{
    writeInt(wopExportPath, to);
    writeString(path, to);
    writeInt(sign ? 1 : 0, to);
    processStderr(&sink); /* sink receives the actual data */
    readInt(from);
}


Path RemoteStore::importPath(bool requireSignature, Source & source)
{
    writeInt(wopImportPath, to);
    /* We ignore requireSignature, since the worker forces it to true
       anyway. */
    
    processStderr(0, &source);
    Path path = readStorePath(from);
    return path;
}


void RemoteStore::buildDerivations(const PathSet & drvPaths)
{
    writeInt(wopBuildDerivations, to);
    writeStringSet(drvPaths, to);
    processStderr();
    readInt(from);
}


void RemoteStore::ensurePath(const Path & path)
{
    writeInt(wopEnsurePath, to);
    writeString(path, to);
    processStderr();
    readInt(from);
}


void RemoteStore::addTempRoot(const Path & path)
{
    writeInt(wopAddTempRoot, to);
    writeString(path, to);
    processStderr();
    readInt(from);
}


void RemoteStore::addIndirectRoot(const Path & path)
{
    writeInt(wopAddIndirectRoot, to);
    writeString(path, to);
    processStderr();
    readInt(from);
}


void RemoteStore::syncWithGC()
{
    writeInt(wopSyncWithGC, to);
    processStderr();
    readInt(from);
}


Roots RemoteStore::findRoots()
{
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


void RemoteStore::collectGarbage(GCAction action, const PathSet & pathsToDelete,
    bool ignoreLiveness, PathSet & result, unsigned long long & bytesFreed)
{
    result.clear();
    bytesFreed = 0;
    writeInt(wopCollectGarbage, to);
    writeInt(action, to);
    writeStringSet(pathsToDelete, to);
    writeInt(ignoreLiveness, to);
    
    processStderr();
    
    result = readStringSet(from);

    /* Ugh - NAR integers are 64 bits, but read/writeInt() aren't. */
    unsigned int lo = readInt(from);
    unsigned int hi = readInt(from);
    bytesFreed = (((unsigned long long) hi) << 32) | lo;
}


void RemoteStore::processStderr(Sink * sink, Source * source)
{
    unsigned int msg;
    while ((msg = readInt(from)) == STDERR_NEXT
        || msg == STDERR_READ || msg == STDERR_WRITE) {
        if (msg == STDERR_WRITE) {
            string s = readString(from);
            if (!sink) throw Error("no sink");
            (*sink)((const unsigned char *) s.c_str(), s.size());
        }
        else if (msg == STDERR_READ) {
            if (!source) throw Error("no source");
            unsigned int len = readInt(from);
            unsigned char * buf = new unsigned char[len];
            AutoDeleteArray<unsigned char> d(buf);
            (*source)(buf, len);
            writeString(string((const char *) buf, len), to);
        }
        else {
            string s = readString(from);
            writeToStderr((const unsigned char *) s.c_str(), s.size());
        }
    }
    if (msg == STDERR_ERROR)
        throw Error(readString(from));
    else if (msg != STDERR_LAST)
        throw Error("protocol error processing standard error");
}


}
