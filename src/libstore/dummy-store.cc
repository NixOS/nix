#include "store-api.hh"
#include "callback.hh"

namespace nix {

struct DummyStoreConfig : virtual StoreConfig {
    using StoreConfig::StoreConfig;

    DummyStoreConfig(std::string_view scheme, std::string_view authority, const Params & params)
        : StoreConfig(params)
    {
        if (!authority.empty())
            throw UsageError("`%s` store URIs must not contain an authority part %s", scheme, authority);
    }

    const std::string name() override { return "Dummy Store"; }

    std::string doc() override
    {
        return
          #include "dummy-store.md"
          ;
    }

    static std::set<std::string> uriSchemes() {
        return {"dummy"};
    }
};

struct DummyStore : public virtual DummyStoreConfig, public virtual Store
{
    DummyStore(std::string_view scheme, std::string_view authority, const Params & params)
        : StoreConfig(params)
        , DummyStoreConfig(scheme, authority, params)
        , Store(params)
    { }

    DummyStore(const Params & params)
        : DummyStore("dummy", "", params)
    { }

    std::string getUri() override
    {
        return *uriSchemes().begin();
    }

    void queryPathInfoUncached(const StorePath & path,
        Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override
    {
        callback(nullptr);
    }

    /**
     * The dummy store is incapable of *not* trusting! :)
     */
    virtual std::optional<TrustedFlag> isTrustedClient() override
    {
        return Trusted;
    }

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override
    { unsupported("queryPathFromHashPart"); }

    void addToStore(const ValidPathInfo & info, Source & source,
        RepairFlag repair, CheckSigsFlag checkSigs) override
    { unsupported("addToStore"); }

    virtual StorePath addToStoreFromDump(
        Source & dump,
        std::string_view name,
        FileSerialisationMethod dumpMethod = FileSerialisationMethod::NixArchive,
        ContentAddressMethod hashMethod = FileIngestionMethod::NixArchive,
        HashAlgorithm hashAlgo = HashAlgorithm::SHA256,
        const StorePathSet & references = StorePathSet(),
        RepairFlag repair = NoRepair) override
    { unsupported("addToStore"); }

    void narFromPath(const StorePath & path, Sink & sink) override
    { unsupported("narFromPath"); }

    void queryRealisationUncached(const DrvOutput &,
        Callback<std::shared_ptr<const Realisation>> callback) noexcept override
    { callback(nullptr); }

    virtual ref<SourceAccessor> getFSAccessor(bool requireValidPath) override
    { unsupported("getFSAccessor"); }
};

static RegisterStoreImplementation<DummyStore, DummyStoreConfig> regDummyStore;

}
