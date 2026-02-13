#pragma once
///@file

#include "nix/store/store-api.hh"

namespace nix {

/**
 * A wrapper around a `Store` that adds in-memory caching of path info lookups.
 *
 * This uses the decorator pattern - it wraps another store and intercepts
 * `isValidPath` and `queryPathInfo` to check/update an in-memory LRU cache
 * before delegating to the wrapped store.
 *
 * Stores that need to invalidate cache entries (e.g., `LocalStore` when
 * registering or invalidating paths) can be given a pointer to this wrapper's
 * cache via `getPathInfoCachePtr()` and their constructor.
 */
struct PathInfoCachedStore : virtual Store
{
    /**
     * A cache entry for path info lookups.
     */
    struct CacheValue
    {
        /**
         * Time of cache entry creation or update
         */
        std::chrono::time_point<std::chrono::steady_clock> time_point = std::chrono::steady_clock::now();

        /**
         * Null if missing
         */
        std::shared_ptr<const ValidPathInfo> value;

        /**
         * Whether the value is valid as a cache entry. The path may not
         * exist.
         */
        bool isKnownNow();

        /**
         * Past tense, because a path can only be assumed to exists when
         * `isKnownNow() && didExist()`
         */
        inline bool didExist()
        {
            return value != nullptr;
        }
    };

    using Cache = LRUCache<StorePath, CacheValue>;

protected:
    ref<Store> inner;

    /**
     * In-memory LRU cache for path info lookups.
     */
    ref<SharedSync<Cache>> cache;

public:
    PathInfoCachedStore(ref<Store> inner, ref<SharedSync<Cache>> cache);

    /**
     * Hack to allow long-running processes like hydra-queue-runner to
     * occasionally flush their path info cache.
     */
    void clearPathInfoCache()
    {
        cache->lock()->clear();
    }

    /**
     * Check whether a path is valid.
     * Checks the cache first, then delegates to inner store.
     */
    bool isValidPath(const StorePath & path) override;

    /**
     * Query information about a valid path.
     * Checks the cache first, then delegates to inner store and caches the result.
     */
    void
    queryPathInfo(const StorePath & path, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override;

    /**
     * Query the information about a realisation.
     * Delegates to inner store (realisations are not cached in-memory currently).
     */
    void queryRealisation(
        const DrvOutput & id, Callback<std::shared_ptr<const UnkeyedRealisation>> callback) noexcept override;

    /**
     * Get a pointer to the cache for stores that need direct access.
     */
    SharedSync<Cache> * getPathInfoCachePtr()
    {
        return &*cache;
    }

    /**
     * Helper to create a PathInfoCachedStore wrapping an inner store.
     *
     * @param cacheSize Size of the LRU cache
     * @param makeInner Function that takes a cache pointer and returns the inner store
     */
    template<typename MakeInner>
    static ref<PathInfoCachedStore> make(size_t cacheSize, MakeInner && makeInner)
    {
        auto cache = make_ref<SharedSync<Cache>>(cacheSize);
        auto inner = std::forward<MakeInner>(makeInner)(&*cache);
        return make_ref<PathInfoCachedStore>(inner, cache);
    }

    // Forwarding methods for remaining pure virtuals

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override
    {
        return inner->queryPathFromHashPart(hashPart);
    }

    void addToStore(
        const ValidPathInfo & info,
        Source & narSource,
        RepairFlag repair = NoRepair,
        CheckSigsFlag checkSigs = CheckSigs) override
    {
        inner->addToStore(info, narSource, repair, checkSigs);
    }

    StorePath addToStoreFromDump(
        Source & dump,
        std::string_view name,
        FileSerialisationMethod dumpMethod = FileSerialisationMethod::NixArchive,
        ContentAddressMethod hashMethod = ContentAddressMethod::Raw::NixArchive,
        HashAlgorithm hashAlgo = HashAlgorithm::SHA256,
        const StorePathSet & references = StorePathSet(),
        RepairFlag repair = NoRepair) override
    {
        return inner->addToStoreFromDump(dump, name, dumpMethod, hashMethod, hashAlgo, references, repair);
    }

    void registerDrvOutput(const Realisation & output) override
    {
        inner->registerDrvOutput(output);
    }

    ref<SourceAccessor> getFSAccessor(bool requireValidPath = true) override
    {
        return inner->getFSAccessor(requireValidPath);
    }

    std::shared_ptr<SourceAccessor> getFSAccessor(const StorePath & path, bool requireValidPath = true) override
    {
        return inner->getFSAccessor(path, requireValidPath);
    }

    std::optional<TrustedFlag> isTrustedClient() override
    {
        return inner->isTrustedClient();
    }
};

} // namespace nix
