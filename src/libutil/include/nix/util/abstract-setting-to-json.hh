#pragma once
///@file

#include <nlohmann/json.hpp>
#include "nix/util/configuration.hh"
#include "nix/util/json-utils.hh"

namespace nix {
template<typename T>
std::map<std::string, nlohmann::json> BaseSetting<T>::toJSONObject() const
{
    auto obj = AbstractSetting::toJSONObject();
    obj.emplace("value", value);
    obj.emplace("defaultValue", defaultValue);
    obj.emplace("documentDefault", documentDefault);
    return obj;
}
} // namespace nix
