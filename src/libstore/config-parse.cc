#include <nlohmann/json.hpp>

#include "config-parse.hh"
#include "json-utils.hh"

namespace nix::config {

};

namespace nlohmann {

using namespace nix::config;

SettingDescription adl_serializer<SettingDescription>::from_json(const json & json)
{
    auto & obj = getObject(json);
    return {
        .description = getString(valueAt(obj, "description")),
        .defaultValue = getBoolean(valueAt(obj, "documentDefault"))
                            ? (std::optional<nlohmann::json>{valueAt(obj, "defaultValue")})
                            : (std::optional<nlohmann::json>{}),
    };
}

void adl_serializer<SettingDescription>::to_json(json & obj, SettingDescription s)
{
    obj.emplace("description", s.description);
    // obj.emplace("aliases", s.aliases);
    // obj.emplace("experimentalFeature", s.experimentalFeature);

    // Indicate the default value is JSON, rather than a legacy setting
    // boolean or string.
    //
    // TODO remove if we no longer have the legacy setting system / the
    // code handling doc rendering of the settings is decoupled.
    obj.emplace("json", true);

    // Cannot just use `null` because the default value might itself be
    // `null`.
    obj.emplace("documentDefault", s.defaultValue.has_value());

    if (s.defaultValue.has_value())
        obj.emplace("defaultValue", *s.defaultValue);
}

}
