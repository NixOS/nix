#include "nix/fetchers/input-cache.hh"
#include "nix/util/sync.hh"

namespace nix::fetchers {

struct InputCacheImpl : InputCache
{
    Sync<std::map<Input, CachedInput>> cache_;

    std::optional<CachedInput> lookup(const Input & originalInput) const override
    {
        auto cache(cache_.readLock());
        auto i = cache->find(originalInput);
        if (i == cache->end())
            return std::nullopt;
        debug(
            "mapping '%s' to previously seen input '%s' -> '%s",
            originalInput.to_string(),
            i->first.to_string(),
            i->second.lockedInput.to_string());
        return i->second;
    }

    void upsert(Input key, CachedInput cachedInput) override
    {
        cache_.lock()->insert_or_assign(std::move(key), std::move(cachedInput));
    }

    void clear() override
    {
        cache_.lock()->clear();
    }
};

ref<InputCache> InputCache::getCache()
{
    static auto cache = make_ref<InputCacheImpl>();
    return cache;
}

}
