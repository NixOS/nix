#pragma once
///@file

#include <nlohmann/json.hpp>

#include "nix/util/config-abstract.hh"
#include "nix/util/json-impls.hh"
#include "nix/util/experimental-features.hh"

namespace nix {

struct ExperimentalFeatureSettings;

};

namespace nix::config {

struct SettingDescription;

/**
 * Typed version used as source of truth, and for operations like
 * defaulting configurations.
 *
 * It is important that this type support `constexpr` values to avoid
 * running into issues with static initialization order.
 */
template<typename T>
struct SettingInfo
{
    /**
     * The "self reference" is a trick For higher order stuff to work without extra wrappers
     */
    using type = SettingInfo<T>;

    /**
     * Name of the setting, used when parsing configuration maps
     */
    std::string_view name;

    /**
     * Description of the setting. It is used just for documentation.
     */
    std::string_view description;

#if 0
    /**
     * Other names of the setting also used when parsing configuration
     * maps. This is useful for back-compat, etc.
     */
    std::set<std::string_view> aliases;
#endif

    /**
     * `ExperimentalFeature` that must be enabled if the setting is
     * allowed to be used
     */
    std::optional<ExperimentalFeature> experimentalFeature;

    /**
     * Whether to document the default value. (Some defaults are
     * system-specific and should not be documented.)
     */
    bool documentDefault = true;

    /**
     * Describe the setting as a key-value pair (name -> other info).
     * The default value will be rendered to JSON if it is to be
     * documented.
     */
    std::pair<std::string, SettingDescription> describe(const T & def) const;

    std::optional<T>
    parseConfig(const nlohmann::json::object_t & map, const ExperimentalFeatureSettings & xpSettings) const;
};

template<typename T>
struct MakeDefault
{
    T (*makeDefault)();
};

/**
 * For the common case where the defaults are completely independent
 * from one another.
 *
 * @note occasionally, when this is not the case, the defaulting logic
 * can be written more manually instead. This is needed e.g. for
 * `LocaFSStore` in libnixstore.
 */
template<typename T>
struct SettingInfoWithDefault : SettingInfo<T>, MakeDefault<T>
{
    /**
     * The "self reference" is a trick For higher order stuff to work without extra wrappers
     */
    using type = SettingInfoWithDefault<T>;
};

struct SettingDescription;

/**
 * Map of setting names to descriptions of those settings.
 */
using SettingDescriptionMap = std::map<std::string, SettingDescription>;

/**
 * Untyped version used for rendering docs. This is not the source of
 * truth, it is generated from the typed one.
 *
 * @note No `name` field because this is intended to be used as the value type
 * of a map
 */
struct SettingDescription
{
    /**
     * @see SettingInfo::description
     */
    std::string description;

#if 0
    /**
     * @see SettingInfo::aliases
     */
    StringSet aliases;
#endif

    /**
     * @see SettingInfo::experimentalFeature
     */
    std::optional<ExperimentalFeature> experimentalFeature;

    /**
     * A single leaf setting, to be optionally specified by arbitrary
     * value (of some type) or left default.
     */
    struct Single
    {
        /**
         * Optional, for the `SettingInfo::documentDefault = false` case.
         */
        std::optional<nlohmann::json> defaultValue;
    };

    /**
     * A nested settings object
     */
    struct Sub
    {
        /**
         * If `false`, this is just pure namespaceing. If `true`, we
         * have a distinction between `null` and `{}`, meaning
         * enabling/disabling the entire settings group.
         */
        bool nullable = true;

        SettingDescriptionMap map;
    };

    /**
     * Variant for `info` below
     */
    using Info = std::variant<Single, Sub>;

    /**
     * More information about this setting, depending on whether its the
     * single leaf setting or subsettings case
     */
    Info info;
};

} // namespace nix::config

JSON_IMPL(config::SettingDescription)
