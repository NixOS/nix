#ifndef __LOCAL_STORE_H
#define __LOCAL_STORE_H

#include <string>

#include "store-api.hh"


namespace nix {


class Transaction;


/* Nix store and database schema version.  Version 1 (or 0) was Nix <=
   0.7.  Version 2 was Nix 0.8 and 0.9.  Version 3 is Nix 0.10 and
   up. */
const int nixSchemaVersion = 3;


extern string drvsLogDir;


class LocalStore : public StoreAPI
{
public:

    /* Open the database environment.  If `reserveSpace' is true, make
       sure that a big empty file exists in /nix/var/nix/db/reserved.
       If `reserveSpace' is false, delete this file if it exists.  The
       idea is that on normal operation, the file exists; but when we
       run the garbage collector, it is deleted.  This is to ensure
       that the garbage collector has a small amount of disk space
       available, which is required to open the Berkeley DB
       environment. */
    LocalStore(bool reserveSpace);

    ~LocalStore();
    
    /* Implementations of abstract store API methods. */
    
    bool isValidPath(const Path & path);
    
    bool isValidStatePath(const Path & path);
    
    bool isValidComponentOrStatePath(const Path & path);
    
    Substitutes querySubstitutes(const Path & srcPath);

    Hash queryPathHash(const Path & path);
    
    Path queryStatePathDrv(const Path & statePath);

    void queryStoreReferences(const Path & path, PathSet & references, const unsigned int revision);
    
    void queryStateReferences(const Path & storePath, PathSet & stateReferences, const unsigned int revision);
    
    void queryStoreReferrers(const Path & path, PathSet & referrers, const unsigned int revision);
    
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
    
    /////////////////////////////
        
	void setStatePathsInterval(const PathSet & statePath, const IntVector & intervals, bool allZero = false);
	
	IntVector getStatePathsInterval(const PathSet & statePaths);
	
	bool isStateComponent(const Path & storePath);
	
	void storePathRequisites(const Path & storeOrstatePath, const bool includeOutputs, PathSet & paths, const bool withComponents, const bool withState, const unsigned int revision);
	
	void setStateRevisions(const RevisionClosure & revisions, const Path & rootStatePath, const string & comment);
	
	bool queryStateRevisions(const Path & statePath, RevisionClosure & revisions, RevisionClosureTS & timestamps, const unsigned int revision);
	
	bool queryAvailableStateRevisions(const Path & statePath, RevisionInfos & revisions);
	
	Snapshots commitStatePath(const Path & statePath);
	
	Path queryDeriver(const Path & path);
	
	PathSet queryDerivers(const Path & storePath, const string & identifier, const string & user);		//should these be in here ????
	
	void scanAndUpdateAllReferences(const Path & statePath, const bool recursive);
	
	bool getSharedWith(const Path & statePath1, Path & statePath2);
	
	PathSet toNonSharedPathSet(const PathSet & statePaths);
	
	void revertToRevision(const Path & statePath, const unsigned int revision_arg, const bool recursive);
	
	void shareState(const Path & from, const Path & to, const bool snapshot);
	
	void unShareState(const Path & path, const bool branch, const bool restoreOld);
};


/* Get a transaction object. */
void createStoreTransaction(Transaction & txn);

/* Copy a path recursively. */
void copyPath(const Path & src, const Path & dst);

/* Register a substitute. */
void registerSubstitute(const Transaction & txn,
    const Path & srcPath, const Substitute & sub);

/* Deregister all substitutes. */
void clearSubstitutes();

/* Register the validity of a path, i.e., that `path' exists, that the
   paths referenced by it exists, and in the case of an output path of
   a derivation, that it has been produced by a succesful execution of
   the derivation (or something equivalent).  Also register the hash
   of the file system contents of the path.  The hash must be a
   SHA-256 hash. */
void registerValidPath(const Transaction & txn,
    const Path & component_or_state_path, const Hash & hash, 
    const PathSet & references, const PathSet & stateReferences,
    const Path & deriver, const unsigned int revision);

struct ValidPathInfo 
{
    Path path;
    Path deriver;
    Hash hash;
    PathSet references;
    PathSet stateReferences;
    int unsigned revision;
};

typedef list<ValidPathInfo> ValidPathInfos;

void registerValidPaths(const Transaction & txn,
    const ValidPathInfos & infos);

/* "Fix", or canonicalise, the meta-data of the files in a store path
   after it has been built.  In particular:
   - the last modification date on each file is set to 0 (i.e.,
     00:00:00 1/1/1970 UTC)
   - the permissions are set of 444 or 555 (i.e., read-only with or
     without execute permission; setuid bits etc. are cleared)
   - the owner and group are set to the Nix user and group, if we're
     in a setuid Nix installation. */
void canonicalisePathMetaData(const Path & path);

/* Checks whether a path is valid. */ 
bool isValidPathTxn(const Transaction & txn, const Path & path);

/* Sets the set of outgoing FS (also state) references for a store path.  Use with
   care! 
   
   0 for revision means overwrite the last revision
   */
void setReferences(const Transaction & txn, const Path & store_or_statePath,
    const PathSet & references, const PathSet & stateReferences, const unsigned int revision);

/* Sets the deriver of a store path.  Use with care! */
void setDeriver(const Transaction & txn, const Path & path,
    const Path & deriver);

/* Query the derivers of a state-store path. */
PathSet queryDerivers(const Transaction & txn, const Path & storePath, const string & identifier, const string & user);

/* Delete a value from the nixStore directory. */
void deleteFromStore(const Path & path, unsigned long long & bytesFreed);

/* Delete a value from the nixState directory. */
void deleteFromState(const Path & _path, unsigned long long & bytesFreed);

MakeError(PathInUse, Error);

void verifyStore(bool checkContents);

/* Whether we are in build users mode. */
bool haveBuildUsers();

/* Whether we are root. */
bool amPrivileged();

/* Recursively change the ownership of `path' to the current uid. */
void getOwnership(const Path & path);

/* Like deletePath(), but changes the ownership of `path' using the
   setuid wrapper if necessary (and possible). */
void deletePathWrapped(const Path & path,
    unsigned long long & bytesFreed);

void deletePathWrapped(const Path & path);

/* Adds a new deriver based on storePath to the dbDerivers table */
void addStateDeriver(const Transaction & txn, const Path & storePath, const Path & deriver);

bool isStateComponentTxn(const Transaction & txn, const Path & path);

bool isStateDrvPathTxn(const Transaction & txn, const Path & drvPath);

bool isStateDrv(const Derivation & drv);


//TODO CHECK IF THESE DONT BELONG HERE, REFACTOR CODE, EG MOVE FUNCTIONS AROUND

void queryAllValidPathsTxn(const Transaction & txn, PathSet & allComponentPaths, PathSet & allStatePaths);
bool isValidStatePathTxn(const Transaction & txn, const Path & path);

void queryXReferencesTxn(const Transaction & txn, const Path & path, PathSet & references, const bool component_or_state, const unsigned int revision, const unsigned int timestamp = 0);

void setStateComponentReferencesTxn(const Transaction & txn, const Path & statePath, const Strings & references, const unsigned int revision, const unsigned int timestamp);
void setStateStateReferencesTxn(const Transaction & txn, const Path & statePath, const Strings & references, const unsigned int revision, const unsigned int timestamp);

void queryStoreReferrersTxn(const Transaction & txn, const Path & storePath, PathSet & referrers, const unsigned int revision);
void queryStateReferrersTxn(const Transaction & txn, const Path & storePath, PathSet & stateReferrers, const unsigned int revision);

Path queryStatePathDrvTxn(const Transaction & txn, const Path & statePath);
void storePathRequisitesTxn(const Transaction & txn, const Path & storeOrstatePath, const bool includeOutputs, PathSet & paths, const bool withComponents, const bool withState, const unsigned int revision);
void setStateRevisionsTxn(const Transaction & txn, const RevisionClosure & revisions, const Path & rootStatePath, const string & comment);

bool isValidPathTxn(const Transaction & txn, const Path & path);
bool isValidStatePathTxn(const Transaction & txn, const Path & path);

void setSolidStateReferencesTxn(const Transaction & txn, const Path & statePath, const PathSet & paths);
bool querySolidStateReferencesTxn(const Transaction & txn, const Path & statePath, PathSet & paths);

void shareStateTxn(const Transaction & txn, const Path & from, const Path & to, const bool snapshot);
void unShareStateTxn(const Transaction & txn, const Path & path, const bool branch, const bool restoreOld);

PathSet toNonSharedPathSetTxn(const Transaction & txn, const PathSet & statePaths);
Path toNonSharedPathTxn(const Transaction & txn, const Path & statePath);

/*
 * Returns a pathset with all paths (including itself) that eventually share the same statePath
 */
PathSet getSharedWithPathSetRecTxn(const Transaction & txn, const Path & statePath);

void ensurePathTxn(const Transaction & txn, const Path & path);
IntVector getStatePathsIntervalTxn(const Transaction & txn, const PathSet & statePaths);

bool queryStateRevisionsTxn(const Transaction & txn, const Path & statePath, RevisionClosure & revisions, RevisionClosureTS & timestamps, const unsigned int revision);
void setStatePathsIntervalTxn(const Transaction & txn, const PathSet & statePath, const IntVector & intervals, bool allZero = false);
bool querySharedStateTxn(const Transaction & txn, const Path & statePath, Path & shared_with);


}


#endif /* !__LOCAL_STORE_H */
