#pragma once
///@file

#include <nlohmann/json.hpp>

#include "nix/store/config-parse.hh"
#include "nix/util/util.hh"
#include "nix/util/json-utils.hh"
#include "nix/main/configuration.hh"

namespace nix::config {

template<typename T>
std::optional<T>
SettingInfo<T>::parseConfig(const nlohmann::json::object_t & map, const ExperimentalFeatureSettings & xpSettings) const
{
    const nlohmann::json * p = get(map, name);
    if (p && experimentalFeature)
        xpSettings.require(*experimentalFeature);
    return p ? (std::optional<T>{p->get<T>()}) : std::nullopt;
}

template<typename T>
std::pair<std::string, SettingDescription> SettingInfo<T>::describe(const T & def) const
{
    return {
        std::string{name},
        SettingDescription{
            .description = stripIndentation(description),
            .experimentalFeature = experimentalFeature,
            .info =
                SettingDescription::Single{
                    .defaultValue =
                        documentDefault ? (std::optional{nlohmann::json(def)}) : (std::optional<nlohmann::json>{}),
                },
        },
    };
}

/**
 * Look up the setting's name in a map, falling back on the default if
 * it does not exist.
 */
#define CONFIG_ROW(FIELD) .FIELD = descriptions.FIELD.parseConfig(params, xpSettings)

#define DESCRIBE_ROW(FIELD)                                            \
    {                                                                  \
        descriptions.FIELD.describe(descriptions.FIELD.makeDefault()), \
    }

#define APPLY_ROW(FIELD) .FIELD = parsed.FIELD ? *parsed.FIELD : descriptions.FIELD.makeDefault()

#define MAKE_PARSE(CAPITAL, LOWER, FIELDS)                                            \
    static CAPITAL##T<config::OptionalValue> LOWER##Parse(                            \
        const StoreReference::Params & params,                                        \
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings) \
    {                                                                                 \
        constexpr auto & descriptions = LOWER##Descriptions;                          \
        return {FIELDS(CONFIG_ROW)};                                                  \
    }

#define MAKE_APPLY_PARSE(CAPITAL, LOWER, FIELDS)                                                   \
    static CAPITAL##T<config::PlainValue> LOWER##ApplyParse(const StoreReference::Params & params) \
    {                                                                                              \
        auto parsed = LOWER##Parse(params);                                                        \
        constexpr auto & descriptions = LOWER##Descriptions;                                       \
        return {FIELDS(APPLY_ROW)};                                                                \
    }

/**
 * Version for separate defaults
 */
#define DESCRIBE_ROW_SEP_DEFAULTS(FIELD)             \
    {                                                \
        descriptions.FIELD.describe(defaults.FIELD), \
    }

/**
 * Version for separate defaults
 */
#define APPLY_ROW_SEP_DEFAULTS(FIELD) .FIELD = parsed.FIELD.value_or(std::move(defaults.FIELD))

/**
 * Version for separate defaults
 */
#define MAKE_APPLY_PARSE_SEP_DEFAULTS(CAPITAL, LOWER, FIELDS)                                      \
    static CAPITAL##T<config::PlainValue> LOWER##ApplyParse(const StoreReference::Params & params) \
    {                                                                                              \
        auto defaults = LOWER##Defaults();                                                         \
        auto parsed = LOWER##Parse(params);                                                        \
        return {FIELDS(APPLY_ROW_SEP_DEFAULTS)};                                                   \
    }

} // namespace nix::config
