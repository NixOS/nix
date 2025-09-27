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
        };
    }
};

struct MemorySourceAccessor;

/**
 * Enough of the Dummy Store exposed for sake of writing unit tests
 */
struct DummyStore : virtual Store
{
    using Config = DummyStoreConfig;

    ref<const Config> config;

    struct PathInfoAndContents
    {
        UnkeyedValidPathInfo info;
        ref<MemorySourceAccessor> contents;
    };

    /**
     * This is map conceptually owns the file system objects for each
     * store object.
     */
    boost::concurrent_flat_map<StorePath, PathInfoAndContents> contents;

    /**
     * The build trace maps the pair of a content-addressing (fixed or
     * floating) derivations an one of its output to a
     * (content-addressed) store object.
     */
    boost::concurrent_flat_map<DrvOutput, ref<UnkeyedRealisation>> buildTrace;

    DummyStore(ref<const Config> config)
        : Store{*config}
        , config(config)
    {
    }
};

ref<Store> DummyStoreConfig::openStore() const
{
    return openDummyStore();
}

} // namespace nix
