#include "nix/store/path-info-cache-store.hh"
#include "nix/store/globals.hh"
#include "nix/util/callback.hh"

namespace nix {

bool PathInfoCachedStore::CacheValue::isKnownNow()
{
    std::chrono::duration ttl = didExist() ? std::chrono::seconds(settings.ttlPositiveNarInfoCache)
                                           : std::chrono::seconds(settings.ttlNegativeNarInfoCache);

    return std::chrono::steady_clock::now() < time_point + ttl;
}

PathInfoCachedStore::PathInfoCachedStore(ref<Store> inner, ref<SharedSync<Cache>> cache)
    : Store{inner->config}
    , inner{inner}
    , cache{cache}
{
}

bool PathInfoCachedStore::isValidPath(const StorePath & path)
{
    {
        auto cacheRef(cache->lock());
        auto * entry = cacheRef->getOrNullptr(path);
        if (entry && entry->isKnownNow()) {
            stats.narInfoReadAverted++;
            return entry->didExist();
        }
    }

    bool valid = inner->isValidPath(path);

    if (!valid) {
        auto cacheRef(cache->lock());
        cacheRef->upsert(path, CacheValue{.value = nullptr});
    }

    return valid;
}

void PathInfoCachedStore::queryPathInfo(
    const StorePath & storePath, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept
{
    try {
        {
            auto cacheRef(cache->lock());
            auto * entry = cacheRef->getOrNullptr(storePath);
            if (entry && entry->isKnownNow()) {
                stats.narInfoReadAverted++;
                auto info = entry->value;
                return callback(std::move(info));
            }
        }
    } catch (...) {
        return callback.rethrow();
    }

    auto callbackPtr = std::make_shared<decltype(callback)>(std::move(callback));

    inner->queryPathInfo(
        storePath, {[this, storePath, callbackPtr](std::future<std::shared_ptr<const ValidPathInfo>> fut) {
            try {
                auto info = fut.get();
                {
                    auto cacheRef(cache->lock());
                    cacheRef->upsert(storePath, CacheValue{.value = info});
                }
                (*callbackPtr)(std::move(info));
            } catch (...) {
                callbackPtr->rethrow();
            }
        }});
}

void PathInfoCachedStore::queryRealisation(
    const DrvOutput & id, Callback<std::shared_ptr<const UnkeyedRealisation>> callback) noexcept
{
    inner->queryRealisation(id, std::move(callback));
}

} // namespace nix
