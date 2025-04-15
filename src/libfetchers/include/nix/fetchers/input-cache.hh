#include "fetchers.hh"

namespace nix::fetchers {

struct InputCache
{
    struct CachedResult
    {
        ref<SourceAccessor> accessor;
        Input resolvedInput;
        Input lockedInput;
    };

    CachedResult getAccessor(ref<Store> store, const Input & originalInput, bool useRegistries);

    struct CachedInput
    {
        Input lockedInput;
        ref<SourceAccessor> accessor;
    };

    virtual std::optional<CachedInput> lookup(const Input & originalInput) const = 0;

    virtual void upsert(Input key, CachedInput cachedInput) = 0;

    virtual void clear() = 0;

    static ref<InputCache> create();
};

}
