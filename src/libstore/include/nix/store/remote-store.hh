#pragma once
///@file

#include <limits>
#include <string>

#include "nix/store/store-api.hh"
#include "nix/store/gc-store.hh"
#include "nix/store/log-store.hh"

namespace nix {

class Pipe;
class Pid;
struct FdSink;
struct FdSource;
template<typename T>
class Pool;
class RemoteFSAccessor;

struct RemoteStoreConfig : virtual StoreConfig
{
    using StoreConfig::StoreConfig;

    const Setting<int> maxConnections{
        this, 1, "max-connections", "Maximum number of concurrent connections to the Nix daemon."};

    const Setting<unsigned int> maxConnectionAge{
        this,
        std::numeric_limits<unsigned int>::max(),
        "max-connection-age",
        "Maximum age of a connection before it is closed."};
};

/**
 * \todo RemoteStore is a misnomer - should be something like
 * DaemonStore.
 */
struct RemoteStore : public virtual Store, public virtual GcStore, public virtual LogStore
{
    using Config = RemoteStoreConfig;

    const Config & config;

    RemoteStore(const Config & config);

    /* Implementations of abstract store API methods. */

    bool isValidPathUncached(const StorePath & path) override;

    StorePathSet queryValidPaths(const StorePathSet & paths, SubstituteFlag maybeSubstitute = NoSubstitute) override;

    StorePathSet queryAllValidPaths() override;

    void queryPathInfoUncached(
        const StorePath & path, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override;

    void queryReferrers(const StorePath & path, StorePathSet & referrers) override;

    StorePathSet queryValidDerivers(const StorePath & path) override;

    StorePathSet queryDerivationOutputs(const StorePath & path) override;

    std::map<std::string, std::optional<StorePath>>
    queryPartialDerivationOutputMap(const StorePath & path, Store * evalStore = nullptr) override;
    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override;

    StorePathSet querySubstitutablePaths(const StorePathSet & paths) override;

    void querySubstitutablePathInfos(const StorePathCAMap & paths, SubstitutablePathInfos & infos) override;

    /**
     * Add a content-addressable store path. `dump` will be drained.
     */
    ref<const ValidPathInfo> addCAToStore(
        Source & dump,
        std::string_view name,
        ContentAddressMethod caMethod,
        HashAlgorithm hashAlgo,
        const StorePathSet & references,
        RepairFlag repair);

    /**
     * Add a content-addressable store path. `dump` will be drained.
     */
    StorePath addToStoreFromDump(
        Source & dump,
        std::string_view name,
        FileSerialisationMethod dumpMethod = FileSerialisationMethod::NixArchive,
        ContentAddressMethod hashMethod = FileIngestionMethod::NixArchive,
        HashAlgorithm hashAlgo = HashAlgorithm::SHA256,
        const StorePathSet & references = StorePathSet(),
        RepairFlag repair = NoRepair) override;

    void addToStore(const ValidPathInfo & info, Source & nar, RepairFlag repair, CheckSigsFlag checkSigs) override;

    void addMultipleToStore(Source & source, RepairFlag repair, CheckSigsFlag checkSigs) override;

    void
    addMultipleToStore(PathsSource && pathsToCopy, Activity & act, RepairFlag repair, CheckSigsFlag checkSigs) override;

    void registerDrvOutput(const Realisation & info) override;

    void queryRealisationUncached(
        const DrvOutput &, Callback<std::shared_ptr<const UnkeyedRealisation>> callback) noexcept override;

    void
    buildPaths(const std::vector<DerivedPath> & paths, BuildMode buildMode, std::shared_ptr<Store> evalStore) override;

    std::vector<KeyedBuildResult> buildPathsWithResults(
        const std::vector<DerivedPath> & paths, BuildMode buildMode, std::shared_ptr<Store> evalStore) override;

    BuildResult buildDerivation(const StorePath & drvPath, const BasicDerivation & drv, BuildMode buildMode) override;

    void ensurePath(const StorePath & path) override;

    void addTempRoot(const StorePath & path) override;

    Roots findRoots(bool censor) override;

    void collectGarbage(const GCOptions & options, GCResults & results) override;

    void optimiseStore() override;

    bool verifyStore(bool checkContents, RepairFlag repair) override;

    /**
     * The default instance would schedule the work on the client side, but
     * for consistency with `buildPaths` and `buildDerivation` it should happen
     * on the remote side.
     *
     * We make this fail for now so we can add implement this properly later
     * without it being a breaking change.
     */
    void repairPath(const StorePath & path) override
    {
        unsupported("repairPath");
    }

    void addSignatures(const StorePath & storePath, const StringSet & sigs) override;

    MissingPaths queryMissing(const std::vector<DerivedPath> & targets) override;

    void addBuildLog(const StorePath & drvPath, std::string_view log) override;

    std::optional<std::string> getVersion() override;

    void connect() override;

    unsigned int getProtocol() override;

    std::optional<TrustedFlag> isTrustedClient() override;

    void flushBadConnections();

    struct Connection;

    ref<Connection> openConnectionWrapper();

protected:

    virtual ref<Connection> openConnection() = 0;

    void initConnection(Connection & conn);

    ref<Pool<Connection>> connections;

    virtual void setOptions(Connection & conn);

    void setOptions() override;

    struct ConnectionHandle;

    ConnectionHandle getConnection();

    friend struct ConnectionHandle;

    virtual ref<SourceAccessor> getFSAccessor(bool requireValidPath = true) override;

    virtual std::shared_ptr<SourceAccessor>
    getFSAccessor(const StorePath & path, bool requireValidPath = true) override;

    virtual void narFromPath(const StorePath & path, Sink & sink) override;

private:

    /**
     * Same as the default implemenation of `RemoteStore::getFSAccessor`, but with a more preceise return type.
     */
    ref<RemoteFSAccessor> getRemoteFSAccessor(bool requireValidPath = true);

    std::atomic_bool failed{false};

    void copyDrvsFromEvalStore(const std::vector<DerivedPath> & paths, std::shared_ptr<Store> evalStore);
};

} // namespace nix
