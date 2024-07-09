#include "store-api.hh"
#include "callback.hh"
#include "memory-source-accessor.hh"
#include "archive.hh"

namespace nix {

struct DummyStoreConfig : virtual StoreConfig {
    using StoreConfig::StoreConfig;

    const std::string name() override { return "Dummy Store"; }

    Setting<bool> readOnly{this,
        true,
        "read-only",
        R"(
          Make any sort of write fail instead of succeeding.
          No additional memory will be used, because no information needs to be stored.
        )"};

    std::string doc() override
    {
        return
          #include "dummy-store.md"
          ;
    }
};

struct DummyStore : public virtual DummyStoreConfig, public virtual Store
{
    DummyStore(std::string_view scheme, std::string_view authority, const Params & params)
        : DummyStore(params)
    {
        if (!authority.empty())
            throw UsageError("`%s` store URIs must not contain an authority part %s", scheme, authority);
    }

    DummyStore(const Params & params)
        : StoreConfig(params)
        , DummyStoreConfig(params)
        , Store(params)
        , contents(make_ref<MemorySourceAccessor>())
    { }

    ref<MemorySourceAccessor> contents;

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

    static std::set<std::string> uriSchemes() {
        return {"dummy"};
    }

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override
    { unsupported("queryPathFromHashPart"); }

    void addToStore(const ValidPathInfo & info, Source & source,
        RepairFlag repair, CheckSigsFlag checkSigs) override
    { unsupported("addToStore"); }

    StorePath addToStoreFromDump(
        Source & source,
        std::string_view name,
        FileSerialisationMethod dumpMethod = FileSerialisationMethod::NixArchive,
        ContentAddressMethod hashMethod = FileIngestionMethod::NixArchive,
        HashAlgorithm hashAlgo = HashAlgorithm::SHA256,
        const StorePathSet & references = StorePathSet(),
        RepairFlag repair = NoRepair) override
    {
        if (readOnly) unsupported("addToStoreFromDump");

        auto temp = make_ref<MemorySourceAccessor>();

        {
            MemorySink tempSink{*temp};

            // TODO factor this out into `restorePath`, same todo on it.
            switch (dumpMethod) {
            case FileSerialisationMethod::Recursive:
                parseDump(tempSink, source);
                break;
            case FileSerialisationMethod::Flat: {
                // Replace root dir with file so next part succeeds.
                temp->root = MemorySourceAccessor::File::Regular {};
                tempSink.createRegularFile("/", [&](auto & sink) {
                   source.drainInto(sink);
                });
                break;
            }
            }
        }

        auto hash = hashPath(
            {temp, CanonPath::root},
            hashMethod.getFileIngestionMethod(),
            hashAlgo).first;

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

        contents->open(CanonPath(printStorePath(dstPath)), std::move(temp->root) );

        return dstPath;
    }

    void narFromPath(const StorePath & path, Sink & sink) override
    { unsupported("narFromPath"); }

    void queryRealisationUncached(const DrvOutput &,
        Callback<std::shared_ptr<const Realisation>> callback) noexcept override
    { callback(nullptr); }

    ref<SourceAccessor> getFSAccessor(bool requireValidPath) override
    {
        return this->contents;
    }
};

static RegisterStoreImplementation<DummyStore, DummyStoreConfig> regDummyStore;

}
