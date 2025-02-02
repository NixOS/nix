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

    // Indicate this is a JSON default, not a
    obj.emplace("json", true);

    // Cannot just use `null` because the default value might itself be
    // `null`.
    obj.emplace("documentDefault", s.defaultValue.has_value());

    if (s.defaultValue.has_value())
        obj.emplace("defaultValue", *s.defaultValue);
}

}
