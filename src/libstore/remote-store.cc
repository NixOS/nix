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


Path readXPath(Source & from, const bool canBeStore, const bool canBeState)
{
    Path path = readString(from);
    
    if(canBeStore && canBeState)
    	assertStoreOrStatePath(path);
    else if(canBeStore)
    	assertStorePath(path);
    else if(canBeState)
    	assertStatePath(path);
    	
    return path;
}
Path readStorePath(Source & from)
{
	return readXPath(from, true, false);
}	
Path readStatePath(Source & from)
{
	return readXPath(from, false, true);
}
Path readStoreOrStatePath(Source & from)
{
	return readXPath(from, true, true);
}

PathSet readXPaths(Source & from, const bool canBeStore, const bool canBeState)
{
    PathSet paths = readStringSet(from);
    for (PathSet::iterator i = paths.begin(); i != paths.end(); ++i){
    	if(canBeStore && canBeState)
    		assertStoreOrStatePath(*i);
	    else if(canBeStore)
	    	assertStorePath(*i);
	    else if(canBeState)
	    	assertStatePath(*i);
    }
    return paths;
}
PathSet readStorePaths(Source & from)
{
	return readXPaths(from, true, false);
}
PathSet readStatePaths(Source & from)
{
	return readXPaths(from, false, true);
}



RemoteStore::RemoteStore()
{
    string remoteMode = getEnv("NIX_REMOTE");

	debug(format("Client remoteMode: '%1%'") % remoteMode);
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
        unsigned int magic = readInt(from);
        if (magic != WORKER_MAGIC_2) throw Error("protocol mismatch");

        daemonVersion = readInt(from);
        if (GET_PROTOCOL_MAJOR(daemonVersion) != GET_PROTOCOL_MAJOR(PROTOCOL_VERSION))
            throw Error("Nix daemon protocol version not supported");
        writeInt(PROTOCOL_VERSION, to);
        processStderr();

    } catch (Error & e) {
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
    } catch (...) {
        ignoreException();
    }
}


void RemoteStore::setOptions()
{
    writeInt(wopSetOptions, to);
    writeInt(keepFailed, to);
    writeInt(keepGoing, to);
    writeInt(tryFallback, to);
    writeInt(verbosity, to);
    writeInt(maxBuildJobs, to);
    writeInt(maxSilentTime, to);
    if (GET_PROTOCOL_MINOR(daemonVersion) >= 2)
        writeInt(useBuildHook, to);
    processStderr();
}


bool RemoteStore::isValidPath(const Path & path)
{
    writeInt(wopIsValidPath, to);
    writeString(path, to);
    processStderr();
    unsigned int reply = readInt(from);
    return reply != 0;
}

bool RemoteStore::isValidStatePath(const Path & path)
{
	writeInt(wopIsValidStatePath, to);
    writeString(path, to);
    processStderr();
    unsigned int reply = readInt(from);
    return reply != 0;
}
    
bool RemoteStore::isValidComponentOrStatePath(const Path & path)
{
	writeInt(wopIsValidComponentOrStatePath, to);
    writeString(path, to);
    processStderr();
    unsigned int reply = readInt(from);
    return reply != 0;
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

Path RemoteStore::queryStatePathDrv(const Path & statePath)
{
	writeInt(wopQueryStatePathDrv, to);
	writeString(statePath, to);
    processStderr();
    return readString(from);
}

void RemoteStore::queryStoreReferences(const Path & path,
    PathSet & references, const unsigned int revision)
{
    writeInt(wopQueryStoreReferences, to);
    writeString(path, to);
    writeBigUnsignedInt(revision, to);
    processStderr();
    PathSet references2 = readStorePaths(from);
    references.insert(references2.begin(), references2.end());
}

void RemoteStore::queryStateReferences(const Path & path,
    PathSet & stateReferences, const unsigned int revision)
{
    writeInt(wopQueryStateReferences, to);
    writeString(path, to);
    writeBigUnsignedInt(revision, to);
    processStderr();
	PathSet stateReferences2 = readStatePaths(from);
	stateReferences.insert(stateReferences2.begin(), stateReferences2.end());
}


void RemoteStore::queryStoreReferrers(const Path & path,
    PathSet & referrers, const unsigned int revision)
{
    writeInt(wopQueryStoreReferrers, to);
    writeString(path, to);
    writeBigUnsignedInt(revision, to);
    processStderr();
    PathSet referrers2 = readStorePaths(from);
    referrers.insert(referrers2.begin(), referrers2.end());
   
}

void RemoteStore::queryStateReferrers(const Path & path, 
	PathSet & stateReferrers, const unsigned int revision)
{
	writeInt(wopQueryStateReferrers, to);
    writeString(path, to);
    writeBigUnsignedInt(revision, to);
    processStderr();
    PathSet stateReferrers2 = readStatePaths(from);
    stateReferrers.insert(stateReferrers2.begin(), stateReferrers2.end());
}

Path RemoteStore::queryDeriver(const Path & path)
{
    writeInt(wopQueryDeriver, to);
    writeString(path, to);
    processStderr();
    Path drvPath = readString(from);
    if (drvPath != "") assertStorePath(drvPath);
    return drvPath;
}


PathSet RemoteStore::querySubstitutablePaths()
{
    throw Error("not implemented");
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
    return readStorePath(from);
}


Path RemoteStore::addTextToStore(const string & suffix, const string & s,
    const PathSet & references)
{
    writeInt(wopAddTextToStore, to);
    writeString(suffix, to);
    writeString(s, to);
    writeStringSet(references, to);
    processStderr();
    return readStorePath(from);
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
    return readStorePath(from);
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

PathSet RemoteStore::queryDerivers(const Path & storePath)
{
	writeInt(wopQueryDerivers, to);
	writeString(storePath, to);
	processStderr();
	return readStorePaths(from);
}

void RemoteStore::setStatePathsInterval(const Path & statePath, const CommitIntervals & intervals)
{
    writeInt(wopSetStatePathsInterval, to);
	writeString(statePath, to);
	writeCommitIntervals(intervals, to);
	processStderr();
	readInt(from);
}

CommitIntervals RemoteStore::getStatePathsInterval(const Path & statePath)
{
	writeInt(wopGetStatePathsInterval, to);
	writeString(statePath, to);
	processStderr();
	return readCommitIntervals(from);
}

bool RemoteStore::isStateComponent(const Path & path)
{
	writeInt(wopIsStateComponent, to);
	writeString(path, to);
	processStderr();
	unsigned int reply = readInt(from);
    return reply != 0;
}

void RemoteStore::storePathRequisites(const Path & storeOrstatePath, const bool includeOutputs, PathSet & paths, const bool withComponents, const bool withState, const unsigned int revision)
{
	writeInt(wopStorePathRequisites, to);
	writeString(storeOrstatePath, to);
	writeInt(includeOutputs ? 1 : 0, to);
	writeStringSet(paths, to);
	writeInt(withComponents ? 1 : 0, to);
	writeInt(withState ? 1 : 0, to);
	writeBigUnsignedInt(revision, to);	
	processStderr();
	readInt(from);
}

void RemoteStore::setStateRevisions(const RevisionClosure & revisions, const Path & rootStatePath, const string & comment)
{
	writeInt(wopSetStateRevisions, to);
	writeRevisionClosure(revisions, to);
	writeString(rootStatePath, to);
	writeString(comment, to);	
	processStderr();
	readInt(from);
}

bool RemoteStore::queryStateRevisions(const Path & statePath, RevisionClosure & revisions, RevisionClosureTS & timestamps, const unsigned int revision)
{
	writeInt(wopQueryStateRevisions, to);
	writeString(statePath, to); 
	writeBigUnsignedInt(revision, to);
	processStderr();	
	RevisionClosure revisions2 = readRevisionClosure(from);
	RevisionClosureTS timestamps2 = readRevisionClosureTS(from);
	revisions = revisions2;												
	timestamps = timestamps2;											//TODO Is this ok?? Maybe COPY BY VALUE ??
	unsigned int reply = readInt(from);
    return reply != 0;
}

bool RemoteStore::queryAvailableStateRevisions(const Path & statePath, RevisionInfos & revisions)
{
	writeInt(wopQueryAvailableStateRevisions, to);
	writeString(statePath, to);
	processStderr();
	RevisionInfos revisions2 = readRevisionInfos(from);			
	revisions = revisions2;												
	unsigned int reply = readInt(from);
    return reply != 0;	
}

Snapshots RemoteStore::commitStatePath(const Path & statePath)
{
	writeInt(wopCommitStatePath, to);
	writeString(statePath, to);
	processStderr();
	return readSnapshots(from);
}	

void RemoteStore::scanAndUpdateAllReferences(const Path & statePath, const bool recursive)
{
	writeInt(wopScanAndUpdateAllReferences, to);
	writeString(statePath, to);
	writeInt(recursive ? 1 : 0, to);
	processStderr();
	readInt(from);
}

bool RemoteStore::getSharedWith(const Path & statePath1, Path & statePath2)
{
	writeInt(wopGetSharedWith, to);
	writeString(statePath1, to);
	processStderr();
	statePath2 = readString(from);
	unsigned int reply = readInt(from);
    return reply != 0;
}

PathSet RemoteStore::toNonSharedPathSet(const PathSet & statePaths)
{
	writeInt(wopToNonSharedPathSet, to);
	writeStringSet(statePaths, to);
	processStderr();
	return readStatePaths(from);
}

void RemoteStore::revertToRevision(const Path & statePath, const unsigned int revision_arg, const bool recursive)
{
	writeInt(wopRevertToRevision, to);
	writeString(statePath, to);
	writeBigUnsignedInt(revision_arg, to);
	writeInt(recursive ? 1 : 0, to);
	processStderr();
	readInt(from);
}

void RemoteStore::shareState(const Path & from_arg, const Path & to_arg, const bool snapshot)
{
	writeInt(wopShareState, to);
	writeString(from_arg, to);
	writeString(to_arg, to);
	writeInt(snapshot ? 1 : 0, to);
	processStderr();
	readInt(from);
}

void RemoteStore::unShareState(const Path & path, const bool branch, const bool restoreOld)
{
	writeInt(wopUnShareState, to);
	writeString(path, to);
	writeInt(branch ? 1 : 0, to);
	writeInt(restoreOld ? 1 : 0, to);
	processStderr();
	readInt(from);
}

Path RemoteStore::lookupStatePath(const Path & storePath, const string & identifier, const string & user)
{
	writeInt(wopLookupStatePath, to);
	writeString(storePath, to);
	writeString(identifier, to);
	writeString(user, to);
	processStderr();
	return readStatePath(from);	
}

void RemoteStore::setStateOptions(const Path & statePath, const string & user, const string & group, int chmod, const string & runtimeArgs)
{
	writeInt(wopSetStateOptions, to);
	writeString(statePath, to);
	writeString(user, to);
	writeString(group, to);
	writeInt(chmod, to);
	writeString(runtimeArgs, to);
	processStderr();
	readInt(from);
}

void RemoteStore::getStateOptions(const Path & statePath, string & user, string & group, int & chmod, string & runtimeArgs)
{
	writeInt(wopGetStateOptions, to);
	writeString(statePath, to);
	processStderr();
	user = readString(from);
	group = readString(from);
	chmod = readInt(from);
	runtimeArgs = readString(from);
	readInt(from);
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
