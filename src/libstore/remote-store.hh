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

    void queryReferences(const Path & path, PathSet & references, const int revision);

	void queryStateReferences(const Path & storePath, PathSet & stateReferences, const int revision);

	void queryAllReferences(const Path & path, PathSet & allReferences, const int revision);

    void queryReferrers(const Path & path, PathSet & referrers, const int revision);
    
    void queryStateReferrers(const Path & path, PathSet & stateReferrers, const int revision);

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
        
    void setStatePathsInterval(const PathSet & statePath, const vector<int> & intervals, bool allZero = false);
	
	vector<int> getStatePathsInterval(const PathSet & statePaths);
	
	bool isStateComponent(const Path & path);
	
	bool isStateDrvPath(const Path & drvpath);
	
	bool isStateDrv(const Derivation & drv);
	
	void storePathRequisites(const Path & storeOrstatePath, const bool includeOutputs, PathSet & paths, const bool & withComponents, const bool & withState, const int revision);

	void setStateRevisions(const Path & statePath, const RevisionNumbersSet & revisions);
	
	bool queryStateRevisions(const Path & statePath, RevisionNumbersSet & revisions, const int revision);
	
	bool queryAvailableStateRevisions(const Path & statePath, RevisionNumbers & revisions);
    
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
