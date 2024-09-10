#pragma once
//@file

#include <nlohmann/json_fwd.hpp>

#include "config-abstract.hh"
#include "json-impls.hh"

namespace nix::config {

struct SettingDescription;

/**
 * Typed version used as source of truth, and for operations like
 * defaulting configurations.
 */
template<typename T>
struct SettingInfo
{
    /**
     * Name of the setting, used when parsing configuration maps
     */
    std::string name;

    /**
     * Description of the setting. It is used just for documentation.
     */
    std::string description;

#if 0
    /**
     * Other names of the setting also used when parsing configuration
     * maps. This is useful for back-compat, etc.
     */
    std::set<std::string> aliases;

    /**
     * `ExperimentalFeature` that must be enabled if the setting is
     * allowed to be used
     */
    std::optional<ExperimentalFeature> experimentalFeature;
#endif

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
    std::pair<std::string, SettingDescription> describe(const JustValue<T> & def) const;

    OptValue<T> parseConfig(const nlohmann::json::object_t & map) const;
};

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
    std::set<std::string> aliases;

    /**
     * @see SettingInfo::experimentalFeature
     */
    std::optional<ExperimentalFeature> experimentalFeature;
#endif

    /**
     * Optional, for the `SettingInfo::documentDefault = false` case.
     */
    std::optional<nlohmann::json> defaultValue;
};

/**
 * Map of setting names to descriptions of those settings.
 */
using SettingDescriptionMap = std::map<std::string, SettingDescription>;

}

JSON_IMPL(config::SettingDescription)
