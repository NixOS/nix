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
    
    Hash queryPathHash(const Path & path);

    void queryReferences(const Path & path, PathSet & references);

    void queryReferrers(const Path & path, PathSet & referrers);

    Path queryDeriver(const Path & path);
    
    bool hasSubstitutes(const Path & path);
    
    bool querySubstitutablePathInfo(const Path & path,
        SubstitutablePathInfo & info);
    
    Path addToStore(const Path & srcPath,
        bool recursive = true, string hashAlgo = "sha256",
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
