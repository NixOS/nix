#include "nix/fetchers/input-cache.hh"
#include "nix/fetchers/registry.hh"
#include "nix/util/sync.hh"
#include "nix/util/source-path.hh"

namespace nix::fetchers {

InputCache::CachedResult
InputCache::getAccessor(ref<Store> store, const Input & originalInput, UseRegistries useRegistries)
{
    auto fetched = lookup(originalInput);
    Input resolvedInput = originalInput;

    if (!fetched) {
        if (originalInput.isDirect()) {
            auto [accessor, lockedInput] = originalInput.getAccessor(store);
            fetched.emplace(CachedInput{.lockedInput = lockedInput, .accessor = accessor});
        } else {
            if (useRegistries != UseRegistries::No) {
                auto [res, extraAttrs] = lookupInRegistries(store, originalInput, useRegistries);
                resolvedInput = std::move(res);
                fetched = lookup(resolvedInput);
                if (!fetched) {
                    auto [accessor, lockedInput] = resolvedInput.getAccessor(store);
                    fetched.emplace(
                        CachedInput{.lockedInput = lockedInput, .accessor = accessor, .extraAttrs = extraAttrs});
                }
                upsert(resolvedInput, *fetched);
            } else {
                throw Error(
                    "'%s' is an indirect flake reference, but registry lookups are not allowed",
                    originalInput.to_string());
            }
        }
        upsert(originalInput, *fetched);
    }

    debug("got tree '%s' from '%s'", fetched->accessor, fetched->lockedInput.to_string());

    return {fetched->accessor, resolvedInput, fetched->lockedInput, fetched->extraAttrs};
}

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

ref<InputCache> InputCache::create()
{
    return make_ref<InputCacheImpl>();
}

} // namespace nix::fetchers
