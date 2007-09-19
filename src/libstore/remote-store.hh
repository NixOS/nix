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

    Substitutes querySubstitutes(const Path & path);

    bool hasSubstitutes(const Path & path);
    
    Hash queryPathHash(const Path & path);
    
    Path queryStatePathDrv(const Path & statePath);

    void queryReferences(const Path & path, PathSet & references, const unsigned int revision);

	void queryStateReferences(const Path & storePath, PathSet & stateReferences, const unsigned int revision);

    void queryReferrers(const Path & path, PathSet & referrers, const unsigned int revision);
    
    void queryStateReferrers(const Path & path, PathSet & stateReferrers, const unsigned int revision);

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
        
    void setStatePathsInterval(const PathSet & statePath, const IntVector & intervals, bool allZero = false);
	
	IntVector getStatePathsInterval(const PathSet & statePaths);
	
	bool isStateComponent(const Path & path);
	
	void storePathRequisites(const Path & storeOrstatePath, const bool includeOutputs, PathSet & paths, const bool withComponents, const bool withState, const unsigned int revision);

	void setStateRevisions(const RevisionClosure & revisions, const Path & rootStatePath, const string & comment);
	
	bool queryStateRevisions(const Path & statePath, RevisionClosure & revisions, RevisionClosureTS & timestamps, const unsigned int revision);
	
	bool queryAvailableStateRevisions(const Path & statePath, RevisionInfos & revisions);

	Snapshots commitStatePath(const Path & statePath);
	
	Path queryDeriver(const Path & path);
	
	PathSet queryDerivers(const Path & storePath, const string & identifier, const string & user);
	
	void scanAndUpdateAllReferences(const Path & statePath, const bool recursive);
	
	bool getSharedWith(const Path & statePath1, Path & statePath2);
	
	PathSet toNonSharedPathSet(const PathSet & statePaths);
	
	void revertToRevision(const Path & statePath, const unsigned int revision_arg, const bool recursive);
	
	void shareState(const Path & from, const Path & to, const bool snapshot);
	
	void unShareState(const Path & path, const bool copyFromOld);
	
private:
    AutoCloseFD fdSocket;
    FdSink to;
    FdSource from;
    Pid child;

    void processStderr(Sink * sink = 0, Source * source = 0);

    void forkSlave();
    
    void connectToDaemon();
};


}


#endif /* !__REMOTE_STORE_H */
