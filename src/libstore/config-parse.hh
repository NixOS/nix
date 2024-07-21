#pragma once
//@file

#include <nlohmann/json_fwd.hpp>

#include "config-abstract.hh"
#include "util.hh"

namespace nix::config {

template<typename T>
struct SettingInfo
{
    std::string name;
    std::string description;
    bool documentDefault = true;

    std::optional<JustValue<T>> parseConfig(const nlohmann::json::object_t & map) const;
};

}
