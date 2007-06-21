#ifndef __LOCAL_STORE_H
#define __LOCAL_STORE_H

#include <string>

#include "store-api.hh"


namespace nix {


class Transaction;


/* Nix store and database schema version.  Version 1 (or 0) was Nix <=
   0.7.  Version 2 was Nix 0.8 and 0.8.  Version 3 is Nix 0.10 and
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

    Substitutes querySubstitutes(const Path & srcPath);

    Hash queryPathHash(const Path & path);

    void queryReferences(const Path & path, PathSet & references);
    
    void queryStateReferences(const Path & storePath, PathSet & stateReferences);
    
    void queryReferrers(const Path & path, PathSet & referrers);
    
    void queryStateReferrers(const Path & path, PathSet & stateReferrers);

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
	
	bool isStateDrv(Derivation drv);
	
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
    const Path & path, const Hash & hash, 
    const PathSet & references, const PathSet & stateReferences,
    const Path & deriver);

struct ValidPathInfo 
{
    Path path;
    Path deriver;
    Hash hash;
    PathSet references;
    PathSet stateReferences;
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
   care! */
void setReferences(const Transaction & txn, const Path & path,
    const PathSet & references, const PathSet & stateReferences);

/* Sets the deriver of a store path.  Use with care! */
void setDeriver(const Transaction & txn, const Path & path,
    const Path & deriver);

/* Query the deriver of a store path.  Return the empty string if no
   deriver has been set. */
Path queryDeriver(const Transaction & txn, const Path & path);

/* Query the derivers of a state-store path. Gives an error (TODO?) if no
   deriver has been set. */
PathSet queryDerivers(const Transaction & txn, const Path & storePath, const string & identifier, const string & user);

/* TODO */
PathSet queryDeriversStatePath(const Transaction & txn, const Path & storePath, const string & identifier, const string & user);

/* Delete a value from the nixStore directory. */
void deleteFromStore(const Path & path, unsigned long long & bytesFreed);

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

/* TODO */
void addStateDeriver(const Transaction & txn, const Path & storePath, const Path & deriver);

/* TODO */
PathSet mergeNewDerivationIntoList(const Path & storepath, const Path & newdrv, const PathSet drvs, bool deleteDrvs = false);

bool isStateComponentTxn(const Transaction & txn, const Path & path);

bool isStateDrvPathTxn(const Transaction & txn, const Path & drvPath);

bool isStateDrvTxn(const Transaction & txn, Derivation drv);
 
}


#endif /* !__LOCAL_STORE_H */
