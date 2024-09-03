#pragma once
//@file

#include <nlohmann/json.hpp>

#include "config-parse.hh"
#include "util.hh"

namespace nix::config {

template<typename T>
OptValue<T> SettingInfo<T>::parseConfig(const nlohmann::json::object_t & map) const
{
    const nlohmann::json * p = get(map, name);
    return {.optValue = p ? (std::optional<T>{p->get<T>()}) : std::nullopt};
}

template<typename T>
std::pair<std::string, SettingDescription> SettingInfo<T>::describe(const JustValue<T> & def) const
{
    return {
        name,
        SettingDescription{
            .description = description,
            .defaultValue =
                documentDefault ? (std::optional<nlohmann::json>{}) : (std::optional{nlohmann::json{def.value}}),
        },
    };
}

/**
 * Look up the setting's name in a map, falling back on the default if
 * it does not exist.
 */
#define CONFIG_ROW(FIELD) .FIELD = descriptions.FIELD.parseConfig(params)

#define APPLY_ROW(FIELD) .FIELD = {.value = parsed.FIELD.optValue.value_or(std::move(defaults.FIELD))}

#define DESC_ROW(FIELD)                              \
    {                                                \
        descriptions.FIELD.describe(defaults.FIELD), \
    }

#define MAKE_PARSE(CAPITAL, LOWER, FIELDS)                                                  \
    static CAPITAL##T<config::OptValue> LOWER##Parse(const StoreReference::Params & params) \
    {                                                                                       \
        constexpr auto & descriptions = LOWER##Descriptions;                                \
        return {FIELDS(CONFIG_ROW)};                                                        \
    }

#define MAKE_APPLY_PARSE(CAPITAL, LOWER, FIELDS)                                                  \
    static CAPITAL##T<config::JustValue> LOWER##ApplyParse(const StoreReference::Params & params) \
    {                                                                                             \
        auto defaults = LOWER##Defaults();                                                        \
        auto parsed = LOWER##Parse(params);                                                       \
        return {FIELDS(APPLY_ROW)};                                                               \
    }

}
