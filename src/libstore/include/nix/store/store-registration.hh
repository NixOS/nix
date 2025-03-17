#pragma once
/**
 * @file
 *
 * Infrastructure for "registering" store implementations. Used by the
 * store implementation definitions themselves but not by consumers of
 * those implementations.
 *
 * Consumers of an arbitrary store from a URL/JSON configuration instead
 * just need the definitions `nix/store/store-open.hh`; those do use this
 * but only as an implementation. Consumers of a specific extra type of
 * store can skip both these, and just use the definition of the store
 * in question directly.
 */

#include "nix/store/store-api.hh"

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
    StringSet uriSchemes;

    /**
     * @note This is a functional pointer for now because this situation:
     *
     *   - We register store types with global initializers
     *
     *   - The default values for some settings maybe depend on the settings globals.
     *
     *   And because the ordering of global initialization is arbitrary,
     *   this is not allowed. For now, we can simply defer actually
     *   creating these maps until we need to later.
     */
    config::SettingDescriptionMap (*configDescriptions)();

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
        std::string_view scheme, std::string_view authorityPath, const StoreConfig::Params & params)>
        parseConfig;
};

struct Implementations
{
private:

    /**
     * The name of this type of store, and a factory for it.
     */
    using Map = std::map<std::string, StoreFactory>;

public:

    static Map & registered();

    template<typename TConfig>
    static void add()
    {
        StoreFactory factory{
            .doc = TConfig::doc(),
            .uriSchemes = TConfig::uriSchemes(),
            .configDescriptions = TConfig::descriptions,
            .experimentalFeature = TConfig::experimentalFeature(),
            .parseConfig = ([](auto scheme, auto uri, auto & params) -> ref<StoreConfig> {
                return make_ref<TConfig>(scheme, uri, params);
            }),
        };
        auto [it, didInsert] = registered().insert({TConfig::name(), std::move(factory)});
        if (!didInsert) {
            throw Error("Already registered store with name '%s'", it->first);
        }
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

} // namespace nix
