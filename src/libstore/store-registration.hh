#pragma once
/**
 * @file
 *
 * Infrastructure for "registering" store implementations. Used by the
 * store implementation definitions themselves but not by consumers of
 * those implementations.
 */

#include "store-api.hh"

namespace nix {

struct StoreFactory
{
    /**
     * Documentation for this type of store.
     */
    std::string doc;

    /**
     * URIs with these schemes should be handled by this factory
     */
    std::set<std::string> uriSchemes;

    config::SettingDescriptionMap configDescriptions;

    /**
     * An experimental feature this type store is gated, if it is to be
     * experimental.
     */
    std::optional<ExperimentalFeature> experimentalFeature;

    /**
     * The `authorityPath` parameter is `<authority>/<path>`, or really
     * whatever comes after `<scheme>://` and before `?<query-params>`.
     */
    std::function<ref<StoreConfig>(
        std::string_view scheme, std::string_view authorityPath, const StoreReference::Params & params)>
        parseConfig;
};

struct Implementations
{
private:

    /**
     * The name of this type of store, and a factory for it.
     */
    using V = std::vector<std::pair<std::string, StoreFactory>>;

public:

    static V * registered;

    template<typename TConfig>
    static void add()
    {
        if (!registered)
            registered = new V{};
        StoreFactory factory{
            .doc = TConfig::doc(),
            .uriSchemes = TConfig::uriSchemes(),
            .configDescriptions = TConfig::descriptions(),
            .experimentalFeature = TConfig::experimentalFeature(),
            .parseConfig = ([](auto scheme, auto uri, auto & params) -> ref<StoreConfig> {
                return make_ref<TConfig>(scheme, uri, params);
            }),
        };
        registered->push_back({TConfig::name(), std::move(factory)});
    }
};

template<typename TConfig>
struct RegisterStoreImplementation
{
    RegisterStoreImplementation()
    {
        Implementations::add<TConfig>();
    }
};

}
