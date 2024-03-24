#pragma once
///@file

#include "ssh-store-config.hh"
#include "store-api.hh"
#include "ssh.hh"
#include "callback.hh"
#include "pool.hh"

namespace nix {

struct LegacySSHStoreConfig : virtual CommonSSHStoreConfig
{
    using CommonSSHStoreConfig::CommonSSHStoreConfig;

    const Setting<Strings> remoteProgram{this, {"nix-store"}, "remote-program",
        "Path to the `nix-store` executable on the remote machine."};

    const Setting<int> maxConnections{this, 1, "max-connections",
        "Maximum number of concurrent SSH connections."};

    const std::string name() override { return "SSH Store"; }

    std::string doc() override;
};

struct LegacySSHStore : public virtual LegacySSHStoreConfig, public virtual Store
{
    // Hack for getting remote build log output.
    // Intentionally not in `LegacySSHStoreConfig` so that it doesn't appear in
    // the documentation
    const Setting<int> logFD{this, -1, "log-fd", "file descriptor to which SSH's stderr is connected"};

    struct Connection;

    std::string host;

    ref<Pool<Connection>> connections;

    SSHMaster master;

    static std::set<std::string> uriSchemes() { return {"ssh"}; }

    LegacySSHStore(const std::string & scheme, const std::string & host, const Params & params);

    ref<Connection> openConnection();

    std::string getUri() override;

    void queryPathInfoUncached(const StorePath & path,
        Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override;

    void addToStore(const ValidPathInfo & info, Source & source,
        RepairFlag repair, CheckSigsFlag checkSigs) override;

    void narFromPath(const StorePath & path, Sink & sink) override;

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override
    { unsupported("queryPathFromHashPart"); }

    StorePath addToStore(
        std::string_view name,
        SourceAccessor & accessor,
        const CanonPath & srcPath,
        ContentAddressMethod method,
        HashAlgorithm hashAlgo,
        const StorePathSet & references,
        PathFilter & filter,
        RepairFlag repair) override
    { unsupported("addToStore"); }

    virtual StorePath addToStoreFromDump(
        Source & dump,
        std::string_view name,
        FileSerialisationMethod dumpMethod = FileSerialisationMethod::Recursive,
        ContentAddressMethod hashMethod = FileIngestionMethod::Recursive,
        HashAlgorithm hashAlgo = HashAlgorithm::SHA256,
        const StorePathSet & references = StorePathSet(),
        RepairFlag repair = NoRepair) override
    { unsupported("addToStore"); }

public:

    BuildResult buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
        BuildMode buildMode) override;

    void buildPaths(const std::vector<DerivedPath> & drvPaths, BuildMode buildMode, std::shared_ptr<Store> evalStore) override;

    void ensurePath(const StorePath & path) override
    { unsupported("ensurePath"); }

    virtual ref<SourceAccessor> getFSAccessor(bool requireValidPath) override
    { unsupported("getFSAccessor"); }

    /**
     * The default instance would schedule the work on the client side, but
     * for consistency with `buildPaths` and `buildDerivation` it should happen
     * on the remote side.
     *
     * We make this fail for now so we can add implement this properly later
     * without it being a breaking change.
     */
    void repairPath(const StorePath & path) override
    { unsupported("repairPath"); }

    void computeFSClosure(const StorePathSet & paths,
        StorePathSet & out, bool flipDirection = false,
        bool includeOutputs = false, bool includeDerivers = false) override;

    StorePathSet queryValidPaths(const StorePathSet & paths,
        SubstituteFlag maybeSubstitute = NoSubstitute) override;

    void connect() override;

    unsigned int getProtocol() override;

    /**
     * The legacy ssh protocol doesn't support checking for trusted-user.
     * Try using ssh-ng:// instead if you want to know.
     */
    std::optional<TrustedFlag> isTrustedClient() override
    {
        return std::nullopt;
    }

    void queryRealisationUncached(const DrvOutput &,
        Callback<std::shared_ptr<const Realisation>> callback) noexcept override
    // TODO: Implement
    { unsupported("queryRealisation"); }
};

}
