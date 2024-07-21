#pragma once
//@file

#include <nlohmann/json.hpp>

#include "config-parse.hh"
#include "util.hh"

namespace nix::config {

template<typename T>
std::optional<JustValue<T>> SettingInfo<T>::parseConfig(const nlohmann::json::object_t & map) const
{
    auto * p = get(map, name);
    return p ? (JustValue<T>{.value = p->get<T>()}) : (std::optional<JustValue<T>>{});
}

/**
 * Look up the setting's name in a map, falling back on the default if
 * it does not exist.
 */
#define CONFIG_ROW(FIELD) .FIELD = descriptions.FIELD.parseConfig(params).value_or(defaults.FIELD)

}
