#pragma once
///@file

#include "nix/store/dummy-store.hh"

#include <boost/unordered/concurrent_flat_map.hpp>

namespace nix {

struct MemorySourceAccessor;
class WholeStoreViewAccessor;

/**
 * Enough of the Dummy Store exposed for sake of writing unit tests
 */
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

    DummyStore(ref<const Config> config)
        : Store{*config}
        , config(config)
    {
    }
};

/**
 * Full implementation of DummyStore.
 * Exposed in the header so it can be inherited from in tests.
 */
class DummyStoreImpl : public DummyStore
{
protected:
    /**
     * This view conceptually just borrows the file systems objects of
     * each store object from `contents`, and combines them together
     * into one store-wide source accessor.
     *
     * This is needed just in order to implement `Store::getFSAccessor`.
     */
    ref<WholeStoreViewAccessor> wholeStoreView;

public:
    using Config = DummyStoreConfig;

    DummyStoreImpl(ref<const Config> config);

    void queryPathInfoUncached(
        const StorePath & path, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override;

    /**
     * The dummy store is incapable of *not* trusting! :)
     */
    virtual std::optional<TrustedFlag> isTrustedClient() override;

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override;

    void addToStore(const ValidPathInfo & info, Source & source, RepairFlag repair, CheckSigsFlag checkSigs) override;

    virtual StorePath addToStoreFromDump(
        Source & source,
        std::string_view name,
        FileSerialisationMethod dumpMethod = FileSerialisationMethod::NixArchive,
        ContentAddressMethod hashMethod = FileIngestionMethod::NixArchive,
        HashAlgorithm hashAlgo = HashAlgorithm::SHA256,
        const StorePathSet & references = StorePathSet(),
        RepairFlag repair = NoRepair) override;

    void registerDrvOutput(const Realisation & output) override;

    void narFromPath(const StorePath & path, Sink & sink) override;

    void queryRealisationUncached(
        const DrvOutput &, Callback<std::shared_ptr<const Realisation>> callback) noexcept override;

    std::shared_ptr<SourceAccessor> getFSAccessor(const StorePath & path, bool requireValidPath) override;

    ref<SourceAccessor> getFSAccessor(bool requireValidPath) override;
};

} // namespace nix
