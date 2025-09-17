#include "nix/store/store-api.hh"

namespace nix {

struct DummyStoreConfig : public std::enable_shared_from_this<DummyStoreConfig>, virtual StoreConfig
{
    using StoreConfig::StoreConfig;

    DummyStoreConfig(std::string_view scheme, std::string_view authority, const Params & params)
        : StoreConfig(params)
    {
        if (!authority.empty())
            throw UsageError("`%s` store URIs must not contain an authority part %s", scheme, authority);
    }

    static const std::string name()
    {
        return "Dummy Store";
    }

    static std::string doc();

    static StringSet uriSchemes()
    {
        return {"dummy"};
    }

    ref<Store> openStore() const override;

    StoreReference getReference() const override
    {
        return {
            .variant =
                StoreReference::Specified{
                    .scheme = *uriSchemes().begin(),
                },
        };
    }
};

} // namespace nix
