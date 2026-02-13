#pragma once
///@file

#include "nix/store/binary-cache-store.hh"
#include "nix/store/nar-info-disk-cache.hh"

namespace nix {

/**
 * A wrapper around a `BinaryCacheStore` that adds local disk caching
 * of `NarInfo` and realisation lookups.
 *
 * This uses the decorator pattern - it wraps another store and intercepts
 * `isValidPath`, `queryPathInfo`, and `queryRealisation` to check/update the
 * disk cache before delegating to the wrapped store.
 *
 * Methods like `narFromPath`, `addSignatures`, `getFSAccessor` are NOT overridden
 * because they internally call `queryPathInfo`, which should go through this
 * wrapper's disk cache logic.
 */
class DiskCachedBinaryCacheStore : public virtual BinaryCacheStore
{
protected:
    ref<BinaryCacheStore> inner;
    ref<NarInfoDiskCache> diskCache;

    /**
     * Get the cache key (URI) for this store.
     */
    std::string cacheUri();

public:
    DiskCachedBinaryCacheStore(ref<BinaryCacheStore> inner, ref<NarInfoDiskCache> diskCache);

    void init() override;

protected:
    // Cache-aware overrides
    bool isValidPath(const StorePath & path) override;

    void
    queryPathInfo(const StorePath & path, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override;

    void queryRealisation(
        const DrvOutput & id, Callback<std::shared_ptr<const UnkeyedRealisation>> callback) noexcept override;

    void writeNarInfo(ref<NarInfo> narInfo) override;

public:
    void registerDrvOutput(const Realisation & info) override;

protected:
    // Backend storage methods - delegate to inner store
    bool fileExists(const std::string & path) override;

    void upsertFile(
        const std::string & path, RestartableSource & source, const std::string & mimeType, uint64_t sizeHint) override;

    void getFile(const std::string & path, Sink & sink) override;

    void getFile(const std::string & path, Callback<std::optional<std::string>> callback) noexcept override;

public:
    std::optional<TrustedFlag> isTrustedClient() override;
};

} // namespace nix
