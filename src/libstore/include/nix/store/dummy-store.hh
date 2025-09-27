#pragma once
///@file

#include "nix/store/store-api.hh"

#include <boost/unordered/concurrent_flat_map.hpp>

namespace nix {

struct DummyStore;

struct DummyStoreConfig : public std::enable_shared_from_this<DummyStoreConfig>, virtual StoreConfig
{
    DummyStoreConfig(const Params & params)
        : StoreConfig(params)
    {
        // Disable caching since this a temporary in-memory store.
        pathInfoCacheSize = 0;
    }

    DummyStoreConfig(std::string_view scheme, std::string_view authority, const Params & params)
        : DummyStoreConfig(params)
    {
        if (!authority.empty())
            throw UsageError("`%s` store URIs must not contain an authority part %s", scheme, authority);
    }

    Setting<bool> readOnly{
        this,
        true,
        "read-only",
        R"(
          Make any sort of write fail instead of succeeding.
          No additional memory will be used, because no information needs to be stored.
        )"};

    static const std::string name()
    {
        return "Dummy Store";
    }

    static std::string doc();

    static StringSet uriSchemes()
    {
        return {"dummy"};
    }

    /**
     * Same as `openStore`, just with a more precise return type.
     */
    ref<DummyStore> openDummyStore() const;

    ref<Store> openStore() const override;

    StoreReference getReference() const override
    {
        return {
            .variant =
                StoreReference::Specified{
                    .scheme = *uriSchemes().begin(),
                },
            .params = getQueryParams(),
        };
    }
};

} // namespace nix
