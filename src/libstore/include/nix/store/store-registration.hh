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

    fun<ref<StoreConfig>(const ParsedURL & uri, const Store::Config::Params & params)> parseConfig;

    /**
     * Just for dumping the defaults. Kind of awkward this exists,
     * because it means we cannot require fields to be manually
     * specified so easily.
     */
    fun<ref<StoreConfig>()> getConfig;
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
            .parseConfig = ([](const ParsedURL & uri, const auto & params) -> ref<StoreConfig> {
                if constexpr (std::is_constructible_v<TConfig, ParsedURL, StoreConfig::Params>) {
                    return make_ref<TConfig>(uri, params);
                } else if constexpr (std::is_constructible_v<TConfig, ParsedURL::Authority, StoreConfig::Params>) {
                    if (!uri.authority || !uri.renderPath().empty())
                        throw UsageError("Failed to parse store reference: '%s'", uri.to_string());
                    return make_ref<TConfig>(*uri.authority, params);
                } else {
                    auto authorityPath = uri.authority ? uri.authority->to_string() : "";
                    authorityPath += uri.renderPath(true);
                    if constexpr (std::is_constructible_v<TConfig, std::filesystem::path, StoreConfig::Params>) {
                        auto path =
                            authorityPath.empty()
                                ? std::filesystem::path{}
                                : canonPath(urlPathToPath(
                                      splitString<std::vector<std::string>>(percentDecode(authorityPath), "/")));
                        return make_ref<TConfig>(std::move(path), params);
                    } else {
                        return make_ref<TConfig>(uri.scheme, authorityPath, params);
                    }
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
