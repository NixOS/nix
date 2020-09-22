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
struct ConnectionHandle;

struct RemoteStoreConfig : virtual StoreConfig
{
    using StoreConfig::StoreConfig;

    const Setting<int> maxConnections{(StoreConfig*) this, 1,
            "max-connections", "maximum number of concurrent connections to the Nix daemon"};

    const Setting<unsigned int> maxConnectionAge{(StoreConfig*) this, std::numeric_limits<unsigned int>::max(),
            "max-connection-age", "number of seconds to reuse a connection"};
};

/* FIXME: RemoteStore is a misnomer - should be something like
   DaemonStore. */
class RemoteStore : public virtual Store, public virtual RemoteStoreConfig
{
public:

    virtual bool sameMachine() = 0;

    RemoteStore(const Params & params);

    /* Implementations of abstract store API methods. */

    bool isValidPathUncached(StorePathOrDesc path) override;

    StorePathSet queryValidPaths(const StorePathSet & paths,
        SubstituteFlag maybeSubstitute = NoSubstitute) override;

    StorePathSet queryAllValidPaths() override;

    void queryPathInfoUncached(StorePathOrDesc,
        Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override;

    void queryReferrers(const StorePath & path, StorePathSet & referrers) override;

    StorePathSet queryValidDerivers(const StorePath & path) override;

    StorePathSet queryDerivationOutputs(const StorePath & path) override;

    std::map<std::string, std::optional<StorePath>> queryPartialDerivationOutputMap(const StorePath & path) override;
    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override;

    StorePathSet querySubstitutablePaths(const StorePathSet & paths) override;

    void querySubstitutablePathInfos(const StorePathSet & paths,
        const std::set<StorePathDescriptor> & caPaths,
        SubstitutablePathInfos & infos) override;

    void addToStore(const ValidPathInfo & info, Source & nar,
        RepairFlag repair, CheckSigsFlag checkSigs) override;

    StorePath addToStore(const string & name, const Path & srcPath,
        FileIngestionMethod method = FileIngestionMethod::Recursive, HashType hashAlgo = htSHA256,
        PathFilter & filter = defaultPathFilter, RepairFlag repair = NoRepair) override;

    StorePath addTextToStore(const string & name, const string & s,
        const StorePathSet & references, RepairFlag repair) override;

    void buildPaths(const std::vector<StorePathWithOutputs> & paths, BuildMode buildMode) override;

    BuildResult buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
        BuildMode buildMode) override;

    void ensurePath(StorePathOrDesc path) override;

    void addTempRoot(const StorePath & path) override;

    void addIndirectRoot(const Path & path) override;

    void syncWithGC() override;

    Roots findRoots(bool censor) override;

    void collectGarbage(const GCOptions & options, GCResults & results) override;

    void optimiseStore() override;

    bool verifyStore(bool checkContents, RepairFlag repair) override;

    void addSignatures(const StorePath & storePath, const StringSet & sigs) override;

    void queryMissing(const std::vector<StorePathWithOutputs> & targets,
        StorePathSet & willBuild, StorePathSet & willSubstitute, StorePathSet & unknown,
        uint64_t & downloadSize, uint64_t & narSize) override;

    void connect() override;

    unsigned int getProtocol() override;

    void flushBadConnections();

    struct Connection
    {
        AutoCloseFD fd;
        FdSink to;
        FdSource from;
        unsigned int daemonVersion;
        std::chrono::time_point<std::chrono::steady_clock> startTime;

        virtual ~Connection();

        std::exception_ptr processStderr(Sink * sink = 0, Source * source = 0, bool flush = true);
    };

    ref<Connection> openConnectionWrapper();

protected:

    virtual ref<Connection> openConnection() = 0;

    void initConnection(Connection & conn);

    ref<Pool<Connection>> connections;

    virtual void setOptions(Connection & conn);

    ConnectionHandle getConnection();

    friend struct ConnectionHandle;

    virtual ref<FSAccessor> getFSAccessor() override;

    virtual void narFromPath(StorePathOrDesc pathOrDesc, Sink & sink) override;

private:

    std::atomic_bool failed{false};

};

struct UDSRemoteStoreConfig : virtual LocalFSStoreConfig, virtual RemoteStoreConfig
{
    UDSRemoteStoreConfig(const Store::Params & params)
        : StoreConfig(params)
        , LocalFSStoreConfig(params)
        , RemoteStoreConfig(params)
    {
    }

    UDSRemoteStoreConfig()
        : UDSRemoteStoreConfig(Store::Params({}))
    {
    }

    const std::string name() override { return "Local Daemon Store"; }
};

class UDSRemoteStore : public LocalFSStore, public RemoteStore, public virtual UDSRemoteStoreConfig
{
public:

    UDSRemoteStore(const Params & params);
    UDSRemoteStore(const std::string scheme, std::string path, const Params & params);

    std::string getUri() override;

    static std::set<std::string> uriSchemes()
    { return {"unix"}; }

    bool sameMachine() override
    { return true; }

    ref<FSAccessor> getFSAccessor() override
    { return LocalFSStore::getFSAccessor(); }

    void narFromPath(StorePathOrDesc path, Sink & sink) override
    { LocalFSStore::narFromPath(path, sink); }

private:

    ref<RemoteStore::Connection> openConnection() override;
    std::optional<std::string> path;
};


}
