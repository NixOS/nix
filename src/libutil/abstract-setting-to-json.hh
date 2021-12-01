#pragma once

#include <nlohmann/json.hpp>
#include "config.hh"

namespace nix {
template<typename T>
std::map<std::string, nlohmann::json> BaseSetting<T>::toJSONObject()
{
    auto obj = AbstractSetting::toJSONObject();
    obj.emplace("value", value);
    obj.emplace("defaultValue", defaultValue);
    obj.emplace("documentDefault", documentDefault);
    return obj;
}
}
