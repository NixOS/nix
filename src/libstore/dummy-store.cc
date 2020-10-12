#include "store-api.hh"
#include "callback.hh"

namespace nix {

struct DummyStoreConfig : virtual StoreConfig {
    using StoreConfig::StoreConfig;

    const std::string name() override { return "Dummy Store"; }
};

struct DummyStore : public Store, public virtual DummyStoreConfig
{
    DummyStore(const std::string scheme, const std::string uri, const Params & params)
        : DummyStore(params)
    { }

    DummyStore(const Params & params)
        : StoreConfig(params)
        , Store(params)
    { }

    string getUri() override
    {
        return *uriSchemes().begin();
    }

    void queryPathInfoUncached(StorePathOrDesc path,
        Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override
    {
        callback(nullptr);
    }

    static std::set<std::string> uriSchemes() {
        return {"dummy"};
    }

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override
    { unsupported("queryPathFromHashPart"); }

    void addToStore(const ValidPathInfo & info, Source & source,
        RepairFlag repair, CheckSigsFlag checkSigs) override
    { unsupported("addToStore"); }

    StorePath addToStore(const string & name, const Path & srcPath,
        FileIngestionMethod method, HashType hashAlgo,
        PathFilter & filter, RepairFlag repair) override
    { unsupported("addToStore"); }

    StorePath addTextToStore(const string & name, const string & s,
        const StorePathSet & references, RepairFlag repair) override
    { unsupported("addTextToStore"); }

    void narFromPath(StorePathOrDesc path, Sink & sink) override
    { unsupported("narFromPath"); }

    void ensurePath(StorePathOrDesc path) override
    { unsupported("ensurePath"); }

    BuildResult buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
        BuildMode buildMode) override
    { unsupported("buildDerivation"); }
};

static RegisterStoreImplementation<DummyStore, DummyStoreConfig> regDummyStore;

}
