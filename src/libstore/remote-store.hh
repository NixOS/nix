#ifndef __REMOTE_STORE_H
#define __REMOTE_STORE_H

#include <string>

#include "store-api.hh"


namespace nix {


class Pipe;
class Pid;
struct FdSink;
struct FdSource;


class RemoteStore : public StoreAPI
{
public:

    RemoteStore();

    ~RemoteStore();
    
    /* Implementations of abstract store API methods. */
    
    bool isValidPath(const Path & path);
    
    bool isValidStatePath(const Path & path);
    
    bool isValidComponentOrStatePath(const Path & path);

    PathSet queryValidPaths();
    
    Hash queryPathHash(const Path & path);
    
    Path queryStatePathDrv(const Path & statePath);

    void queryStoreReferences(const Path & path, PathSet & references, const unsigned int revision);

	void queryStateReferences(const Path & storePath, PathSet & stateReferences, const unsigned int revision);

    void queryStoreReferrers(const Path & path, PathSet & referrers, const unsigned int revision);
    
    void queryStateReferrers(const Path & path, PathSet & stateReferrers, const unsigned int revision);

    Path queryDeriver(const Path & path);
    
    PathSet querySubstitutablePaths();
    
    bool hasSubstitutes(const Path & path);
    
    Path addToStore(const Path & srcPath, bool fixed = false,
        bool recursive = false, string hashAlgo = "",
        PathFilter & filter = defaultPathFilter);

    Path addTextToStore(const string & suffix, const string & s,
        const PathSet & references);

    void exportPath(const Path & path, bool sign,
        Sink & sink);

    Path importPath(bool requireSignature, Source & source);
    
    void buildDerivations(const PathSet & drvPaths);

    void ensurePath(const Path & path);

    void addTempRoot(const Path & path);

    void addIndirectRoot(const Path & path);
    
    void syncWithGC();
    
    Roots findRoots();

    void collectGarbage(GCAction action, const PathSet & pathsToDelete,
        bool ignoreLiveness, PathSet & result, unsigned long long & bytesFreed);
        
    void setStatePathsInterval(const Path & statePath, const CommitIntervals & intervals);
	
	CommitIntervals getStatePathsInterval(const Path & statePath);
	
	bool isStateComponent(const Path & path);
	
	void storePathRequisites(const Path & storeOrstatePath, const bool includeOutputs, PathSet & paths, const bool withComponents, const bool withState, const unsigned int revision);

	void setStateRevisions(const RevisionClosure & revisions, const Path & rootStatePath, const string & comment);
	
	bool queryStateRevisions(const Path & statePath, RevisionClosure & revisions, RevisionClosureTS & timestamps, const unsigned int revision);
	
	bool queryAvailableStateRevisions(const Path & statePath, RevisionInfos & revisions);

	Snapshots commitStatePath(const Path & statePath);
	
	PathSet queryDerivers(const Path & storePath);
	
	void scanAndUpdateAllReferences(const Path & statePath, const bool recursive);
	
	bool getSharedWith(const Path & statePath1, Path & statePath2);
	
	PathSet toNonSharedPathSet(const PathSet & statePaths);
	
	void revertToRevision(const Path & statePath, const unsigned int revision_arg, const bool recursive);
	
	void shareState(const Path & from, const Path & to, const bool snapshot);
	
	void unShareState(const Path & path, const bool branch, const bool restoreOld);
	
	Path lookupStatePath(const Path & storePath, const string & identifier, const string & user);
	
	void setStateOptions(const Path & statePath, const string & user, const string & group, int chmod, const string & runtimeArgs);
	
	void getStateOptions(const Path & statePath, string & user, string & group, int & chmod, string & runtimeArgs);
	
private:
    AutoCloseFD fdSocket;
    FdSink to;
    FdSource from;
    Pid child;
    unsigned int daemonVersion;

    void processStderr(Sink * sink = 0, Source * source = 0);

    void forkSlave();
    
    void connectToDaemon();

    void setOptions();
};


}


#endif /* !__REMOTE_STORE_H */
