#include "local-log-store.hh"

namespace nix {

/**
 * Configuration for `JsonMetaStore`.
 */
struct JsonMetaStoreConfig : virtual LocalFSStoreConfig
{
    JsonMetaStoreConfig(const StringMap & params)
        : StoreConfig(params)
        , LocalFSStoreConfig(params)
    {
    }

    const PathSetting metaDir{this,
        rootDir.get() ? *rootDir.get() + "/nix/var/nix/metadata" : stateDir.get() + "/metadata",
        "meta",
        "directory where Nix will store metadata about store object."};

    const std::string name() override { return "Experimental Local Cache Store"; }

    std::string doc() override;
};

/**
 * Local store that uses JSON files instead of a SQLite database.
 */
class JsonMetaStore
    : public virtual JsonMetaStoreConfig
    , public virtual MixLocalStore
{

public:
    JsonMetaStore(const Params & params);
    JsonMetaStore(const std::string scheme, std::string path, const Params & params);

    std::string getUri() override;

    static std::set<std::string> uriSchemes()
    { return { "json-meta" }; }

private:
    // Overridden methods…

    void queryPathInfoUncached(const StorePath & path,
        Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override;

    void queryRealisationUncached(
        const DrvOutput & drvOutput,
        Callback<std::shared_ptr<const Realisation>> callback) noexcept override;

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override;

    void addToStore(
        const ValidPathInfo & info, Source & source,
        RepairFlag repair, CheckSigsFlag checkSigs) override;

    StorePath addTextToStore(
        std::string_view name,
        std::string_view s,
        const StorePathSet & references,
        RepairFlag repair) override;

    Roots findRoots(bool censor) override;

    void collectGarbage(const GCOptions & options, GCResults & results) override;
};

}
