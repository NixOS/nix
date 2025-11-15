#include "nix/store/store-registration.hh"
#include "nix/util/archive.hh"
#include "nix/util/callback.hh"
#include "nix/util/memory-source-accessor.hh"
#include "nix/store/dummy-store-impl.hh"
#include "nix/store/realisation.hh"

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

ref<Store> DummyStoreConfig::openStore() const
{
    return openDummyStore();
}

struct DummyStoreImpl : DummyStore
{
    using Config = DummyStoreConfig;

    /**
     * This view conceptually just borrows the file systems objects of
     * each store object from `contents`, and combines them together
     * into one store-wide source accessor.
     *
     * This is needed just in order to implement `Store::getFSAccessor`.
     */
    ref<WholeStoreViewAccessor> wholeStoreView = make_ref<WholeStoreViewAccessor>();

    DummyStoreImpl(ref<const Config> config)
        : Store{*config}
        , DummyStore{config}
    {
        wholeStoreView->setPathDisplay(config->storeDir);
    }

    void queryPathInfoUncached(
        const StorePath & path, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override
    {
        if (path.isDerivation()) {
            if (auto accessor_ = getMemoryFSAccessor(path)) {
                ref<MemorySourceAccessor> accessor = ref{std::move(accessor_)};
                /* compute path info on demand */
                auto narHash =
                    hashPath({accessor, CanonPath::root}, FileSerialisationMethod::NixArchive, HashAlgorithm::SHA256);
                auto info = std::make_shared<ValidPathInfo>(path, UnkeyedValidPathInfo{narHash.hash});
                info->narSize = narHash.numBytesDigested;
                info->ca = ContentAddress{
                    .method = ContentAddressMethod::Raw::Text,
                    .hash = hashString(
                        HashAlgorithm::SHA256,
                        std::get<MemorySourceAccessor::File::Regular>(accessor->root->raw).contents),
                };
                callback(std::move(info));
                return;
            }
        } else {
            if (contents.cvisit(path, [&](const auto & kv) {
                    callback(std::make_shared<ValidPathInfo>(StorePath{kv.first}, kv.second.info));
                }))
                return;
        }

        callback(nullptr);
    }

    /**
     * Do this to avoid `queryPathInfoUncached` computing `PathInfo`
     * that we don't need just to return a `bool`.
     */
    bool isValidPathUncached(const StorePath & path) override
    {
        return path.isDerivation() ? derivations.contains(path) : Store::isValidPathUncached(path);
    }

    /**
     * The dummy store is incapable of *not* trusting! :)
     */
    std::optional<TrustedFlag> isTrustedClient() override
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

        auto accessor = make_ref<MemorySourceAccessor>();
        MemorySink tempSink{*accessor};
        parseDump(tempSink, source);
        auto path = info.path;

        if (info.path.isDerivation()) {
            warn("back compat supporting `addToStore` for inserting derivations in dummy store");
            writeDerivation(
                parseDerivation(*this, accessor->readFile(CanonPath::root), Derivation::nameFromPath(info.path)));
            return;
        }

        contents.insert({
            path,
            PathInfoAndContents{
                std::move(info),
                accessor,
            },
        });
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
        if (isDerivation(name))
            throw Error("Do not insert derivation into dummy store with `addToStoreFromDump`");

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
        contents.insert({
            path,
            PathInfoAndContents{
                std::move(info),
                accessor,
            },
        });
        wholeStoreView->addObject(path.to_string(), accessor);

        return path;
    }

    StorePath writeDerivation(const Derivation & drv, RepairFlag repair = NoRepair) override
    {
        auto drvPath = ::nix::writeDerivation(*this, drv, repair, /*readonly=*/true);

        if (!derivations.contains(drvPath) || repair) {
            if (config->readOnly)
                unsupported("writeDerivation");
            derivations.insert({drvPath, drv});
        }

        return drvPath;
    }

    Derivation readDerivation(const StorePath & drvPath) override
    {
        if (std::optional res = getConcurrent(derivations, drvPath))
            return *res;
        else
            throw Error("derivation '%s' is not valid", printStorePath(drvPath));
    }

    /**
     * No such thing as an "invalid derivation" with the dummy store
     */
    Derivation readInvalidDerivation(const StorePath & drvPath) override
    {
        return readDerivation(drvPath);
    }

    void registerDrvOutput(const Realisation & output) override
    {
        auto ref = make_ref<UnkeyedRealisation>(output);
        buildTrace.insert_or_visit({output.id.drvHash, {{output.id.outputName, ref}}}, [&](auto & kv) {
            kv.second.insert_or_assign(output.id.outputName, make_ref<UnkeyedRealisation>(output));
        });
    }

    void queryRealisationUncached(
        const DrvOutput & drvOutput, Callback<std::shared_ptr<const UnkeyedRealisation>> callback) noexcept override
    {
        bool visited = false;
        buildTrace.cvisit(drvOutput.drvHash, [&](const auto & kv) {
            if (auto it = kv.second.find(drvOutput.outputName); it != kv.second.end()) {
                visited = true;
                callback(it->second.get_ptr());
            }
        });

        if (!visited)
            callback(nullptr);
    }

    std::shared_ptr<MemorySourceAccessor> getMemoryFSAccessor(const StorePath & path, bool requireValidPath = true)
    {
        std::shared_ptr<MemorySourceAccessor> res;
        if (path.isDerivation())
            derivations.cvisit(path, [&](const auto & kv) {
                /* compute path info on demand */
                auto res2 = make_ref<MemorySourceAccessor>();
                res2->root = MemorySourceAccessor::File::Regular{
                    .contents = kv.second.unparse(*this, false),
                };
                res = std::move(res2).get_ptr();
            });
        else
            contents.cvisit(path, [&](const auto & kv) { res = kv.second.contents.get_ptr(); });
        return res;
    }

    std::shared_ptr<SourceAccessor> getFSAccessor(const StorePath & path, bool requireValidPath = true) override
    {
        return getMemoryFSAccessor(path, requireValidPath);
    }

    ref<SourceAccessor> getFSAccessor(bool requireValidPath) override
    {
        return wholeStoreView;
    }
};

ref<DummyStore> DummyStore::Config::openDummyStore() const
{
    return make_ref<DummyStoreImpl>(ref{shared_from_this()});
}

static RegisterStoreImplementation<DummyStore::Config> regDummyStore;

} // namespace nix
