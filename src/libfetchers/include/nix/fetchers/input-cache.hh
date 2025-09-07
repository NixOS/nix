#include "nix/fetchers/fetchers.hh"

namespace nix::fetchers {

enum class UseRegistries : int;

struct InputCache
{
    struct CachedResult
    {
        ref<SourceAccessor> accessor;
        Input resolvedInput;
        Input lockedInput;
        Attrs extraAttrs;
    };

    CachedResult getAccessor(ref<Store> store, const Input & originalInput, UseRegistries useRegistries);

    struct CachedInput
    {
        Input lockedInput;
        ref<SourceAccessor> accessor;
        Attrs extraAttrs;
    };

    virtual std::optional<CachedInput> lookup(const Input & originalInput) const = 0;

    virtual void upsert(Input key, CachedInput cachedInput) = 0;

    virtual void clear() = 0;

    static ref<InputCache> create();

    virtual ~InputCache() = default;
};

} // namespace nix::fetchers
