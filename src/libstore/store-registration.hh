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
    std::set<std::string> uriSchemes;
    /**
     * The `authorityPath` parameter is `<authority>/<path>`, or really
     * whatever comes after `<scheme>://` and before `?<query-params>`.
     */
    std::function<std::shared_ptr<StoreConfig>(
        std::string_view scheme, std::string_view authorityPath, const StoreReference::Params & params)>
        parseConfig;
    const Store::Config::Descriptions & configDescriptions;
};

struct Implementations
{
    static std::vector<StoreFactory> * registered;

    template<typename T>
    static void add()
    {
        if (!registered)
            registered = new std::vector<StoreFactory>();
        StoreFactory factory{
            .uriSchemes = T::Config::uriSchemes(),
            .parseConfig = ([](auto scheme, auto uri, auto & params) -> std::shared_ptr<StoreConfig> {
                return std::make_shared<typename T::Config>(scheme, uri, params);
            }),
            .configDescriptions = T::Config::descriptions,
        };
        registered->push_back(factory);
    }
};

template<typename T>
struct RegisterStoreImplementation
{
    RegisterStoreImplementation()
    {
        Implementations::add<T>();
    }
};

}
