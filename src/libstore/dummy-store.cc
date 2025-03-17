#include "dummy-store.hh"
#include "store-registration.hh"
#include "callback.hh"

namespace nix {

DummyStoreConfig::DummyStoreConfig(
    std::string_view scheme, std::string_view authority, const StoreReference::Params & params)
    : StoreConfig{params}
{
    if (!authority.empty())
        throw UsageError("`%s` store URIs must not contain an authority part %s", scheme, authority);
}

std::string DummyStoreConfig::doc()
{
    return
      #include "dummy-store.md"
      ;
}


struct DummyStore : virtual Store
{
    using Config = DummyStoreConfig;

    ref<const Config> config;

    DummyStore(ref<const Config> config)
        : Store{*config}
        , config(config)
    { }

    std::string getUri() override
    {
        return *Config::uriSchemes().begin() + "://";
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

ref<Store> DummyStore::Config::openStore() const
{
    return make_ref<DummyStore>(ref{shared_from_this()});
}

static RegisterStoreImplementation<DummyStore::Config> regDummyStore;

}
