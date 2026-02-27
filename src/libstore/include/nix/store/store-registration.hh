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
#include "nix/util/url.hh"

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
     * An experimental feature this type store is gated, if it is to be
     * experimental.
     */
    std::optional<ExperimentalFeature> experimentalFeature;

    /**
     * The `authorityPath` parameter is `<authority>/<path>`, or really
     * whatever comes after `<scheme>://` and before `?<query-params>`.
     */
    std::function<ref<StoreConfig>(
        std::string_view scheme, std::string_view authorityPath, const Store::Config::Params & params)>
        parseConfig;

    /**
     * Just for dumping the defaults. Kind of awkward this exists,
     * because it means we cannot require fields to be manually
     * specified so easily.
     */
    std::function<ref<StoreConfig>()> getConfig;
};

struct Implementations
{
    using Map = std::map<std::string, StoreFactory>;

    static Map & registered();

    template<typename TConfig>
    static void add()
    {
        StoreFactory factory{
            .doc = TConfig::doc(),
            .uriSchemes = TConfig::uriSchemes(),
            .experimentalFeature = TConfig::experimentalFeature(),
            .parseConfig = ([](auto scheme, auto uri, auto & params) -> ref<StoreConfig> {
                if constexpr (std::is_constructible_v<TConfig, std::filesystem::path, StoreConfig::Params>) {
                    auto path = uri.empty()
                        ? std::filesystem::path{}
                        : canonPath(urlPathToPath(splitString<std::vector<std::string>>(percentDecode(uri), "/")));
                    return make_ref<TConfig>(std::move(path), params);
                } else if constexpr (std::is_constructible_v<TConfig, ParsedURL, StoreConfig::Params>) {
                    return make_ref<TConfig>(parseURL(concatStrings(scheme, "://", uri)), params);
                } else if constexpr (std::is_constructible_v<TConfig, ParsedURL::Authority, StoreConfig::Params>) {
                    return make_ref<TConfig>(ParsedURL::Authority::parse(uri), params);
                } else {
                    return make_ref<TConfig>(scheme, uri, params);
                }
            }),
            .getConfig = ([]() -> ref<StoreConfig> { return make_ref<TConfig>(Store::Config::Params{}); }),
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
