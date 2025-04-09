#include "fetchers.hh"

namespace nix::fetchers {

struct CachedInput
{
    Input lockedInput;
    ref<SourceAccessor> accessor;
};

struct InputCache
{
    virtual std::optional<CachedInput> lookup(const Input & originalInput) const = 0;

    virtual void upsert(Input key, CachedInput cachedInput) = 0;

    virtual void clear() = 0;

    static ref<InputCache> getCache();
};

}
