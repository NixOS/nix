#include "nix/store/store-registration.hh"
#include "nix/util/archive.hh"
#include "nix/util/callback.hh"
#include "nix/util/memory-source-accessor.hh"
#include "nix/store/dummy-store.hh"

#include <boost/unordered/concurrent_flat_map.hpp>

namespace nix {

std::string DummyStoreConfig::doc()
{
    return
#include "dummy-store.md"
        ;
}

namespace {

class WholeStoreViewAccessor : public SourceAccessor
{
    using BaseName = std::string;

    /**
     * Map from store path basenames to corresponding accessors.
     */
    boost::concurrent_flat_map<BaseName, ref<MemorySourceAccessor>> subdirs;

    /**
     * Helper accessor for accessing just the CanonPath::root.
     */
    MemorySourceAccessor rootPathAccessor;

    /**
     * Helper empty accessor.
     */
    MemorySourceAccessor emptyAccessor;

    auto
    callWithAccessorForPath(CanonPath path, std::invocable<MemorySourceAccessor &, const CanonPath &> auto callback)
    {
        if (path.isRoot())
            return callback(rootPathAccessor, path);

        BaseName baseName(*path.begin());
        MemorySourceAccessor * res = nullptr;

        subdirs.cvisit(baseName, [&](const auto & kv) {
            path = path.removePrefix(CanonPath{baseName});
            res = &*kv.second;
        });

        if (!res)
            res = &emptyAccessor;

        return callback(*res, path);
    }

public:
    WholeStoreViewAccessor()
    {
        MemorySink sink{rootPathAccessor};
        sink.createDirectory(CanonPath::root);
    }

    void addObject(std::string_view baseName, ref<MemorySourceAccessor> accessor)
    {
        subdirs.emplace(baseName, std::move(accessor));
    }

    std::string readFile(const CanonPath & path) override
    {
        return callWithAccessorForPath(
            path, [](SourceAccessor & accessor, const CanonPath & path) { return accessor.readFile(path); });
    }

    void readFile(const CanonPath & path, Sink & sink, std::function<void(uint64_t)> sizeCallback) override
    {
        return callWithAccessorForPath(path, [&](SourceAccessor & accessor, const CanonPath & path) {
            return accessor.readFile(path, sink, sizeCallback);
        });
    }

    bool pathExists(const CanonPath & path) override
    {
        return callWithAccessorForPath(
            path, [](SourceAccessor & accessor, const CanonPath & path) { return accessor.pathExists(path); });
    }

    std::optional<Stat> maybeLstat(const CanonPath & path) override
    {
        return callWithAccessorForPath(
            path, [](SourceAccessor & accessor, const CanonPath & path) { return accessor.maybeLstat(path); });
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        return callWithAccessorForPath(
            path, [](SourceAccessor & accessor, const CanonPath & path) { return accessor.readDirectory(path); });
    }

    std::string readLink(const CanonPath & path) override
    {
        return callWithAccessorForPath(
            path, [](SourceAccessor & accessor, const CanonPath & path) { return accessor.readLink(path); });
    }
};

} // namespace

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
    boost::concurrent_flat_map<StorePath, PathInfoAndContents> contents;

    /**
     * This view conceptually just borrows the file systems objects of
     * each store object from `contents`, and combines them together
     * into one store-wide source accessor.
     *
     * This is needed just in order to implement `Store::getFSAccessor`.
     */
    ref<WholeStoreViewAccessor> wholeStoreView = make_ref<WholeStoreViewAccessor>();

    DummyStore(ref<const Config> config)
        : Store{*config}
        , config(config)
    {
        wholeStoreView->setPathDisplay(config->storeDir);
    }

    void queryPathInfoUncached(
        const StorePath & path, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override
    {
        bool visited = contents.cvisit(path, [&](const auto & kv) {
            callback(std::make_shared<ValidPathInfo>(StorePath{kv.first}, kv.second.info));
        });

        if (!visited)
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

        auto accessor = make_ref<MemorySourceAccessor>(std::move(*temp));
        contents.insert(
            {path,
             PathInfoAndContents{
                 std::move(info),
                 accessor,
             }});
        wholeStoreView->addObject(path.to_string(), accessor);
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
        auto accessor = make_ref<MemorySourceAccessor>(std::move(*temp));
        contents.insert(
            {path,
             PathInfoAndContents{
                 std::move(info),
                 accessor,
             }});
        wholeStoreView->addObject(path.to_string(), accessor);

        return path;
    }

    void narFromPath(const StorePath & path, Sink & sink) override
    {
        bool visited = contents.cvisit(path, [&](const auto & kv) {
            const auto & [info, accessor] = kv.second;
            SourcePath sourcePath(accessor);
            dumpPath(sourcePath, sink, FileSerialisationMethod::NixArchive);
        });

        if (!visited)
            throw Error("path '%s' is not valid", printStorePath(path));
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
