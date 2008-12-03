#ifndef __LOCAL_STORE_H
#define __LOCAL_STORE_H

#include <string>

#include <ext/stdio_filebuf.h>

#include "store-api.hh"
#include "util.hh"


namespace nix {


/* Nix store and database schema version.  Version 1 (or 0) was Nix <=
   0.7.  Version 2 was Nix 0.8 and 0.9.  Version 3 is Nix 0.10.
   Version 4 is Nix 0.11.  Version 5 is Nix 0.12*/
const int nixSchemaVersion = 5;


extern string drvsLogDir;


struct OptimiseStats
{
    unsigned long totalFiles;
    unsigned long sameContents;
    unsigned long filesLinked;
    unsigned long long bytesFreed;
    unsigned long long blocksFreed;
    OptimiseStats()
    {
        totalFiles = sameContents = filesLinked = 0;
        bytesFreed = blocksFreed = 0;
    }
};


typedef __gnu_cxx::stdio_filebuf<char> stdio_filebuf;


struct RunningSubstituter
{
    Pid pid;
    boost::shared_ptr<stdio_filebuf> toBuf, fromBuf;
    boost::shared_ptr<std::ostream> to;
    boost::shared_ptr<std::istream> from;
};


class LocalStore : public StoreAPI
{
private:
    bool substitutablePathsLoaded;
    PathSet substitutablePaths;

    typedef std::map<Path, RunningSubstituter> RunningSubstituters;
    RunningSubstituters runningSubstituters;
    
public:

    /* Initialise the local store, upgrading the schema if
       necessary. */
    LocalStore();

    ~LocalStore();
    
    /* Implementations of abstract store API methods. */
    
    bool isValidPath(const Path & path);

    PathSet queryValidPaths();
    
    Hash queryPathHash(const Path & path);

    void queryReferences(const Path & path, PathSet & references);

    void queryReferrers(const Path & path, PathSet & referrers);

    Path queryDeriver(const Path & path);
    
    PathSet querySubstitutablePaths();
    
    bool hasSubstitutes(const Path & path);

    bool querySubstitutablePathInfo(const Path & path,
        SubstitutablePathInfo & info);
    
    bool querySubstitutablePathInfo(const Path & substituter,
        const Path & path, SubstitutablePathInfo & info);
    
    Path addToStore(const Path & srcPath,
        bool recursive = true, HashType hashAlgo = htSHA256,
        PathFilter & filter = defaultPathFilter);

    Path addTextToStore(const string & name, const string & s,
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

    void collectGarbage(const GCOptions & options, GCResults & results);

    /* Delete a path from the Nix store. */
    void deleteFromStore(const Path & path, unsigned long long & bytesFreed,
        unsigned long long & blocksFreed);
    
    /* Optimise the disk space usage of the Nix store by hard-linking
       files with the same contents. */
    void optimiseStore(bool dryRun, OptimiseStats & stats);

    /* Check the integrity of the Nix store. */
    void verifyStore(bool checkContents);

    /* Register the validity of a path, i.e., that `path' exists, that
       the paths referenced by it exists, and in the case of an output
       path of a derivation, that it has been produced by a succesful
       execution of the derivation (or something equivalent).  Also
       register the hash of the file system contents of the path.  The
       hash must be a SHA-256 hash. */
    void registerValidPath(const Path & path,
        const Hash & hash, const PathSet & references, const Path & deriver);

    void registerValidPaths(const ValidPathInfos & infos);

private:

    Path schemaPath;

    /* Lock file used for upgrading. */
    AutoCloseFD globalLock;

    /* !!! The cache can grow very big.  Maybe it should be pruned
       every once in a while. */
    std::map<Path, ValidPathInfo> pathInfoCache;

    /* Store paths for which the referrers file must be purged. */
    PathSet delayedUpdates;

    int getSchema();

    void registerValidPath(const ValidPathInfo & info, bool ignoreValidity = false);

    ValidPathInfo queryPathInfo(const Path & path);

    void rewriteReferrers(const Path & path, bool purge, PathSet referrers);

    void flushDelayedUpdates();
    
    bool queryReferrersInternal(const Path & path, PathSet & referrers);
    
    void invalidatePath(const Path & path);
    
    void upgradeStore12();

    void gcPath(const GCOptions & options, GCResults & results,
        const Path & path);

    void gcPathRecursive(const GCOptions & options,
        GCResults & results, PathSet & done, const Path & path);

    void startSubstituter(const Path & substituter,
        RunningSubstituter & runningSubstituter);
};


/* Copy a path recursively. */
void copyPath(const Path & src, const Path & dst);

/* "Fix", or canonicalise, the meta-data of the files in a store path
   after it has been built.  In particular:
   - the last modification date on each file is set to 0 (i.e.,
     00:00:00 1/1/1970 UTC)
   - the permissions are set of 444 or 555 (i.e., read-only with or
     without execute permission; setuid bits etc. are cleared)
   - the owner and group are set to the Nix user and group, if we're
     in a setuid Nix installation. */
void canonicalisePathMetaData(const Path & path);

void canonicalisePathMetaData(const Path & path, bool recurse);

MakeError(PathInUse, Error);

/* Whether we are in build users mode. */
bool haveBuildUsers();

/* Whether we are root. */
bool amPrivileged();

/* Recursively change the ownership of `path' to the current uid. */
void getOwnership(const Path & path);

/* Like deletePath(), but changes the ownership of `path' using the
   setuid wrapper if necessary (and possible). */
void deletePathWrapped(const Path & path,
    unsigned long long & bytesFreed, unsigned long long & blocksFreed);

void deletePathWrapped(const Path & path);
 
}


#endif /* !__LOCAL_STORE_H */
