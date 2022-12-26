#pragma once
///@file

#include <limits>
#include <string>

#include "store-api.hh"
#include "gc-store.hh"
#include "log-store.hh"


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

    const Setting<int> maxConnections{(StoreConfig*) this, 1, "max-connections",
        "Maximum number of concurrent connections to the Nix daemon."};

    const Setting<unsigned int> maxConnectionAge{(StoreConfig*) this,
        std::numeric_limits<unsigned int>::max(),
        "max-connection-age",
        "Maximum age of a connection before it is closed."};
};

/* FIXME: RemoteStore is a misnomer - should be something like
   DaemonStore. */
class RemoteStore : public virtual RemoteStoreConfig,
    public virtual Store,
    public virtual GcStore,
    public virtual LogStore
{
public:

    RemoteStore(const Params & params);

    /* Implementations of abstract store API methods. */

    bool isValidPathUncached(const StorePath & path) override;

    StorePathSet queryValidPaths(const StorePathSet & paths,
        SubstituteFlag maybeSubstitute = NoSubstitute) override;

    StorePathSet queryAllValidPaths() override;

    void queryPathInfoUncached(const StorePath & path,
        Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override;

    void queryReferrers(const StorePath & path, StorePathSet & referrers) override;

    StorePathSet queryValidDerivers(const StorePath & path) override;

    StorePathSet queryDerivationOutputs(const StorePath & path) override;

    std::map<std::string, std::optional<StorePath>> queryPartialDerivationOutputMap(const StorePath & path) override;
    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override;

    StorePathSet querySubstitutablePaths(const StorePathSet & paths) override;

    void querySubstitutablePathInfos(const StorePathCAMap & paths,
        SubstitutablePathInfos & infos) override;

    /* Add a content-addressable store path. `dump` will be drained. */
    ref<const ValidPathInfo> addCAToStore(
        Source & dump,
        std::string_view name,
        ContentAddressMethod caMethod,
        const StorePathSet & references,
        RepairFlag repair);

    /* Add a content-addressable store path. Does not support references. `dump` will be drained. */
    StorePath addToStoreFromDump(Source & dump, std::string_view name,
        FileIngestionMethod method = FileIngestionMethod::Recursive, HashType hashAlgo = htSHA256, RepairFlag repair = NoRepair, const StorePathSet & references = StorePathSet()) override;

    void addToStore(const ValidPathInfo & info, Source & nar,
        RepairFlag repair, CheckSigsFlag checkSigs) override;

    void addMultipleToStore(
        Source & source,
        RepairFlag repair,
        CheckSigsFlag checkSigs) override;

    void addMultipleToStore(
        PathsSource & pathsToCopy,
        Activity & act,
        RepairFlag repair,
        CheckSigsFlag checkSigs) override;

    StorePath addTextToStore(
        std::string_view name,
        std::string_view s,
        const StorePathSet & references,
        RepairFlag repair) override;

    void registerDrvOutput(const Realisation & info) override;

    void queryRealisationUncached(const DrvOutput &,
        Callback<std::shared_ptr<const Realisation>> callback) noexcept override;

    void buildPaths(const std::vector<DerivedPath> & paths, BuildMode buildMode, std::shared_ptr<Store> evalStore) override;

    std::vector<BuildResult> buildPathsWithResults(
        const std::vector<DerivedPath> & paths,
        BuildMode buildMode,
        std::shared_ptr<Store> evalStore) override;

    BuildResult buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
        BuildMode buildMode) override;

    void ensurePath(const StorePath & path) override;

    void addTempRoot(const StorePath & path) override;

    void addIndirectRoot(const Path & path) override;

    Roots findRoots(bool censor) override;

    void collectGarbage(const GCOptions & options, GCResults & results) override;

    void optimiseStore() override;

    bool verifyStore(bool checkContents, RepairFlag repair) override;

    void addSignatures(const StorePath & storePath, const StringSet & sigs) override;

    void queryMissing(const std::vector<DerivedPath> & targets,
        StorePathSet & willBuild, StorePathSet & willSubstitute, StorePathSet & unknown,
        uint64_t & downloadSize, uint64_t & narSize) override;

    void addBuildLog(const StorePath & drvPath, std::string_view log) override;

    std::optional<std::string> getVersion() override;

    void connect() override;

    unsigned int getProtocol() override;

    std::optional<TrustedFlag> isTrustedClient() override;

    void flushBadConnections();

    struct Connection
    {
        FdSink to;
        FdSource from;
        unsigned int daemonVersion;
        std::optional<TrustedFlag> remoteTrustsUs;
        std::optional<std::string> daemonNixVersion;
        std::chrono::time_point<std::chrono::steady_clock> startTime;

        virtual ~Connection();

        virtual void closeWrite() = 0;

        std::exception_ptr processStderr(Sink * sink = 0, Source * source = 0, bool flush = true);
    };

    ref<Connection> openConnectionWrapper();

protected:

    virtual ref<Connection> openConnection() = 0;

    void initConnection(Connection & conn);

    ref<Pool<Connection>> connections;

    virtual void setOptions(Connection & conn);

    void setOptions() override;

    ConnectionHandle getConnection();

    friend struct ConnectionHandle;

    virtual ref<FSAccessor> getFSAccessor() override;

    virtual void narFromPath(const StorePath & path, Sink & sink) override;

private:

    std::atomic_bool failed{false};

    void copyDrvsFromEvalStore(
        const std::vector<DerivedPath> & paths,
        std::shared_ptr<Store> evalStore);
};


}
