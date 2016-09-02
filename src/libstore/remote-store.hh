#pragma once

#include <limits>
#include <string>

#include "store-api.hh"


namespace nix {


class Pipe;
class Pid;
struct FdSink;
struct FdSource;
template<typename T> class Pool;


/* FIXME: RemoteStore is a misnomer - should be something like
   DaemonStore. */
class RemoteStore : public virtual Store
{
public:

    RemoteStore(const Params & params, size_t maxConnections = std::numeric_limits<size_t>::max());

    /* Implementations of abstract store API methods. */

    bool isValidPathUncached(const Path & path) override;

    PathSet queryValidPaths(const PathSet & paths) override;

    PathSet queryAllValidPaths() override;

    std::shared_ptr<ValidPathInfo> queryPathInfoUncached(const Path & path) override;

    void queryReferrers(const Path & path, PathSet & referrers) override;

    PathSet queryValidDerivers(const Path & path) override;

    PathSet queryDerivationOutputs(const Path & path) override;

    StringSet queryDerivationOutputNames(const Path & path) override;

    Path queryPathFromHashPart(const string & hashPart) override;

    PathSet querySubstitutablePaths(const PathSet & paths) override;

    void querySubstitutablePathInfos(const PathSet & paths,
        SubstitutablePathInfos & infos) override;

    void addToStore(const ValidPathInfo & info, const std::string & nar,
        bool repair, bool dontCheckSigs) override;

    Path addToStore(const string & name, const Path & srcPath,
        bool recursive = true, HashType hashAlgo = htSHA256,
        PathFilter & filter = defaultPathFilter, bool repair = false) override;

    Path addTextToStore(const string & name, const string & s,
        const PathSet & references, bool repair = false) override;

    void buildPaths(const PathSet & paths, BuildMode buildMode) override;

    BuildResult buildDerivation(const Path & drvPath, const BasicDerivation & drv,
        BuildMode buildMode) override;

    void ensurePath(const Path & path) override;

    void addTempRoot(const Path & path) override;

    void addIndirectRoot(const Path & path) override;

    void syncWithGC() override;

    Roots findRoots() override;

    void collectGarbage(const GCOptions & options, GCResults & results) override;

    void optimiseStore() override;

    bool verifyStore(bool checkContents, bool repair) override;

    void addSignatures(const Path & storePath, const StringSet & sigs) override;

protected:

    struct Connection
    {
        FdSink to;
        FdSource from;
        unsigned int daemonVersion;

        ~Connection();

        void processStderr(Sink * sink = 0, Source * source = 0);
    };

    virtual ref<Connection> openConnection() = 0;

    void setOptions(Connection & conn);

    void initConnection(Connection & conn);

private:

    ref<Pool<Connection>> connections;
};

class UDSRemoteStore : public LocalFSStore, public RemoteStore
{
public:

    UDSRemoteStore(const Params & params, size_t maxConnections = std::numeric_limits<size_t>::max());

    std::string getUri() override;

private:

    struct Connection : RemoteStore::Connection
    {
        AutoCloseFD fd;
    };

    ref<RemoteStore::Connection> openConnection() override;
};


}
