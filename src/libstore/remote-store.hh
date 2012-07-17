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

    PathSet queryValidPaths();
    
    ValidPathInfo queryPathInfo(const Path & path);

    Hash queryPathHash(const Path & path);

    void queryReferences(const Path & path, PathSet & references);

    void queryReferrers(const Path & path, PathSet & referrers);

    Path queryDeriver(const Path & path);
    
    PathSet queryDerivationOutputs(const Path & path);
    
    StringSet queryDerivationOutputNames(const Path & path);

    Path queryPathFromHashPart(const string & hashPart);
    
    bool hasSubstitutes(const Path & path);
    
    bool querySubstitutablePathInfo(const Path & path,
        SubstitutablePathInfo & info);
    
    Path addToStore(const Path & srcPath,
        bool recursive = true, HashType hashAlgo = htSHA256,
        PathFilter & filter = defaultPathFilter);

    Path addTextToStore(const string & name, const string & s,
        const PathSet & references);

    void exportPath(const Path & path, bool sign,
        Sink & sink);

    Paths importPaths(bool requireSignature, Source & source);
    
    void buildPaths(const PathSet & paths);

    void ensurePath(const Path & path);

    void addTempRoot(const Path & path);

    void addIndirectRoot(const Path & path);
    
    void syncWithGC();
    
    Roots findRoots();

    void collectGarbage(const GCOptions & options, GCResults & results);
    
    PathSet queryFailedPaths();

    void clearFailedPaths(const PathSet & paths);
    
private:
    AutoCloseFD fdSocket;
    FdSink to;
    FdSource from;
    Pid child;
    unsigned int daemonVersion;
    bool initialised;

    void openConnection(bool reserveSpace = true);

    void processStderr(Sink * sink = 0, Source * source = 0);

    void forkSlave();
    
    void connectToDaemon();

    void setOptions();
};


}


#endif /* !__REMOTE_STORE_H */
