#include "nix/store/store-registration.hh"
#include "nix/util/archive.hh"
#include "nix/util/callback.hh"
#include "nix/util/memory-source-accessor.hh"
#include "nix/store/dummy-store.hh"

namespace nix {

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

    struct PathInfoAndContents
    {
        UnkeyedValidPathInfo info;
        ref<MemorySourceAccessor> contents;
    };

    /**
     * This is map conceptually owns the file system objects for each
     * store object.
     */
    std::map<StorePath, PathInfoAndContents> contents;

    /**
     * This view conceptually just borrows the file systems objects of
     * each store object from `contents`, and combines them together
     * into one store-wide source accessor.
     *
     * This is needed just in order to implement `Store::getFSAccessor`.
     */
    ref<MemorySourceAccessor> wholeStoreView = make_ref<MemorySourceAccessor>();

    DummyStore(ref<const Config> config)
        : Store{*config}
        , config(config)
    {
        wholeStoreView->setPathDisplay(config->storeDir);
        MemorySink sink{*wholeStoreView};
        sink.createDirectory(CanonPath::root);
    }

    void queryPathInfoUncached(
        const StorePath & path, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override
    {
        if (auto it = contents.find(path); it != contents.end())
            callback(std::make_shared<ValidPathInfo>(StorePath{path}, it->second.info));
        else
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
        if (config->readOnly)
            unsupported("addToStore");

        if (repair)
            throw Error("repairing is not supported for '%s' store", config->getHumanReadableURI());

        if (checkSigs)
            throw Error("checking signatures is not supported for '%s' store", config->getHumanReadableURI());

        auto temp = make_ref<MemorySourceAccessor>();
        MemorySink tempSink{*temp};
        parseDump(tempSink, source);
        auto path = info.path;

        auto [it, _] = contents.insert({
            path,
            {
                std::move(info),
                make_ref<MemorySourceAccessor>(std::move(*temp)),
            },
        });

        auto & pathAndContents = it->second;

        bool inserted = wholeStoreView->open(CanonPath(path.to_string()), pathAndContents.contents->root);
        if (!inserted)
            unreachable();
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

        if (repair)
            throw Error("repairing is not supported for '%s' store", config->getHumanReadableURI());

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
        auto narHash = hashPath({temp, CanonPath::root}, FileIngestionMethod::NixArchive, HashAlgorithm::SHA256);

        auto info = ValidPathInfo::makeFromCA(
            *this,
            name,
            ContentAddressWithReferences::fromParts(
                hashMethod,
                std::move(hash),
                {
                    .others = references,
                    // caller is not capable of creating a self-reference, because
                    // this is content-addressed without modulus
                    .self = false,
                }),
            std::move(narHash.first));

        info.narSize = narHash.second.value();

        auto path = info.path;

        auto [it, _] = contents.insert({
            path,
            {
                std::move(info),
                make_ref<MemorySourceAccessor>(std::move(*temp)),
            },
        });

        auto & pathAndContents = it->second;

        bool inserted = wholeStoreView->open(CanonPath(path.to_string()), pathAndContents.contents->root);
        if (!inserted)
            unreachable();

        return path;
    }

    void narFromPath(const StorePath & path, Sink & sink) override
    {
        auto object = contents.find(path);
        if (object == contents.end())
            throw Error("path '%s' is not valid", printStorePath(path));

        const auto & [info, accessor] = object->second;
        SourcePath sourcePath(accessor);
        dumpPath(sourcePath, sink, FileSerialisationMethod::NixArchive);
    }

    void
    queryRealisationUncached(const DrvOutput &, Callback<std::shared_ptr<const Realisation>> callback) noexcept override
    {
        callback(nullptr);
    }

    virtual ref<SourceAccessor> getFSAccessor(bool requireValidPath) override
    {
        return wholeStoreView;
    }
};

ref<Store> DummyStore::Config::openStore() const
{
    return make_ref<DummyStore>(ref{shared_from_this()});
}

static RegisterStoreImplementation<DummyStore::Config> regDummyStore;

} // namespace nix
