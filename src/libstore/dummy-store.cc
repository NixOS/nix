#include "nix/store/store-registration.hh"
#include "nix/util/archive.hh"
#include "nix/util/callback.hh"
#include "nix/util/memory-source-accessor.hh"

namespace nix {

struct DummyStoreConfig : public std::enable_shared_from_this<DummyStoreConfig>, virtual StoreConfig
{
    using StoreConfig::StoreConfig;

    DummyStoreConfig(std::string_view scheme, std::string_view authority, const Params & params)
        : StoreConfig(params)
    {
        if (!authority.empty())
            throw UsageError("`%s` store URIs must not contain an authority part %s", scheme, authority);
    }

    Setting<bool> readOnly{
        this,
        true,
        "read-only",
        R"(
          Make any sort of write fail instead of succeeding.
          No additional memory will be used, because no information needs to be stored.
        )"};

    static const std::string name()
    {
        return "Dummy Store";
    }

    static std::string doc()
    {
        return
#include "dummy-store.md"
            ;
    }

    static StringSet uriSchemes()
    {
        return {"dummy"};
    }

    ref<Store> openStore() const override;

    StoreReference getReference() const override
    {
        return {
            .variant =
                StoreReference::Specified{
                    .scheme = *uriSchemes().begin(),
                },
        };
    }
};

struct DummyStore : virtual Store
{
    using Config = DummyStoreConfig;

    ref<const Config> config;

    ref<MemorySourceAccessor> contents;

    DummyStore(ref<const Config> config)
        : Store{*config}
        , config(config)
        , contents(make_ref<MemorySourceAccessor>())
    {
    }

    void queryPathInfoUncached(
        const StorePath & path, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override
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
    {
        unsupported("queryPathFromHashPart");
    }

    void addToStore(const ValidPathInfo & info, Source & source, RepairFlag repair, CheckSigsFlag checkSigs) override
    {
        unsupported("addToStore");
    }

    StorePath addToStoreFromDump(
        Source & source,
        std::string_view name,
        FileSerialisationMethod dumpMethod = FileSerialisationMethod::NixArchive,
        ContentAddressMethod hashMethod = FileIngestionMethod::NixArchive,
        HashAlgorithm hashAlgo = HashAlgorithm::SHA256,
        const StorePathSet & references = StorePathSet(),
        RepairFlag repair = NoRepair) override
    {
        if (config->readOnly)
            unsupported("addToStoreFromDump");

        auto temp = make_ref<MemorySourceAccessor>();

        {
            MemorySink tempSink{*temp};

            // TODO factor this out into `restorePath`, same todo on it.
            switch (dumpMethod) {
            case FileSerialisationMethod::NixArchive:
                parseDump(tempSink, source);
                break;
            case FileSerialisationMethod::Flat: {
                // Replace root dir with file so next part succeeds.
                temp->root = MemorySourceAccessor::File::Regular{};
                tempSink.createRegularFile(CanonPath::root, [&](auto & sink) { source.drainInto(sink); });
                break;
            }
            }
        }

        auto hash = hashPath({temp, CanonPath::root}, hashMethod.getFileIngestionMethod(), hashAlgo).first;

        auto desc = ContentAddressWithReferences::fromParts(
            hashMethod,
            hash,
            {
                .others = references,
                // caller is not capable of creating a self-reference, because
                // this is content-addressed without modulus
                .self = false,
            });

        auto dstPath = makeFixedOutputPathFromCA(name, desc);

        contents->open(CanonPath(printStorePath(dstPath)), std::move(temp->root));

        return dstPath;
    }

    void narFromPath(const StorePath & path, Sink & sink) override
    {
        unsupported("narFromPath");
    }

    void
    queryRealisationUncached(const DrvOutput &, Callback<std::shared_ptr<const Realisation>> callback) noexcept override
    {
        callback(nullptr);
    }

    virtual ref<SourceAccessor> getFSAccessor(bool requireValidPath) override
    {
        return this->contents;
    }
};

ref<Store> DummyStore::Config::openStore() const
{
    return make_ref<DummyStore>(ref{shared_from_this()});
}

static RegisterStoreImplementation<DummyStore::Config> regDummyStore;

} // namespace nix
