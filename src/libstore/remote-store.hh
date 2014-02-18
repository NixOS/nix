#pragma once

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

    PathSet queryValidPaths(const PathSet & paths);
    
    PathSet queryAllValidPaths();
    
    ValidPathInfo queryPathInfo(const Path & path);

    Hash queryPathHash(const Path & path);

    void queryReferences(const Path & path, PathSet & references);

    void queryReferrers(const Path & path, PathSet & referrers);

    Path queryDeriver(const Path & path);
    
    PathSet queryValidDerivers(const Path & path);

    PathSet queryDerivationOutputs(const Path & path);
    
    StringSet queryDerivationOutputNames(const Path & path);

    Path queryPathFromHashPart(const string & hashPart);
    
    PathSet querySubstitutablePaths(const PathSet & paths);
    
    void querySubstitutablePathInfos(const PathSet & paths,
        SubstitutablePathInfos & infos);
    
    Path addToStore(const Path & srcPath,
        bool recursive = true, HashType hashAlgo = htSHA256,
        PathFilter & filter = defaultPathFilter, bool repair = false);

    Path addTextToStore(const string & name, const string & s,
        const PathSet & references, bool repair = false);

    void exportPath(const Path & path, bool sign,
        Sink & sink);

    Paths importPaths(bool requireSignature, Source & source);
    
    void buildPaths(const PathSet & paths, BuildMode buildMode);

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

    void connectToDaemon();

    void setOptions();
};


}
