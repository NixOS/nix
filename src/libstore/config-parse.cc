#include <nlohmann/json.hpp>

#include "config-parse.hh"

namespace nix::config {

};

namespace nlohmann {

using namespace nix::config;

SettingDescription adl_serializer<SettingDescription>::from_json(const json & json)
{
    // TODO implement if we ever need (testing?)
    assert(false);
}

void adl_serializer<SettingDescription>::to_json(json & obj, SettingDescription s)
{
    obj.emplace("description", s.description);
    // obj.emplace("aliases", s.aliases);
    // obj.emplace("experimentalFeature", s.experimentalFeature);

    // Cannot use `null` because the default value might itself be
    // `null`.
    if (s.defaultValue)
        obj.emplace("defaultValue", *s.defaultValue);
}

}
