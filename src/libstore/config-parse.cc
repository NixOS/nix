#include <nlohmann/json.hpp>

#include "nix/store/config-parse.hh"
#include "nix/util/json-utils.hh"
#include "nix/util/util.hh"

namespace nix::config {

};

namespace nlohmann {

using namespace nix::config;

SettingDescription adl_serializer<SettingDescription>::from_json(const json & json)
{
    auto & obj = getObject(json);
    return {
        .description = getString(valueAt(obj, "description")),
        .experimentalFeature = valueAt(obj, "experimentalFeature").get<std::optional<Xp>>(),
        .info = [&]() -> decltype(SettingDescription::info) {
            if (auto documentDefault = optionalValueAt(obj, "documentDefault")) {
                return SettingDescription::Single{
                    .defaultValue = *documentDefault ? (std::optional<nlohmann::json>{valueAt(obj, "defaultValue")})
                                                     : (std::optional<nlohmann::json>{}),
                };
            } else {
                auto & subObj = getObject(valueAt(obj, "subSettings"));
                return SettingDescription::Sub{
                    .nullable = valueAt(subObj, "nullable"),
                    .map = valueAt(subObj, "map"),
                };
            }
        }(),
    };
}

void adl_serializer<SettingDescription>::to_json(json & obj, const SettingDescription & sd)
{
    obj.emplace("description", sd.description);
    // obj.emplace("aliases", sd.aliases);
    obj.emplace("experimentalFeature", sd.experimentalFeature);

    std::visit(
        overloaded{
            [&](const SettingDescription::Single & single) {
                // Indicate the default value is JSON, rather than a legacy setting
                // boolean or string.
                //
                // TODO remove if we no longer have the legacy setting system / the
                // code handling doc rendering of the settings is decoupled.
                obj.emplace("isJson", true);

                // Cannot just use `null` because the default value might itself be
                // `null`.
                obj.emplace("documentDefault", single.defaultValue.has_value());

                if (single.defaultValue.has_value())
                    obj.emplace("defaultValue", *single.defaultValue);
            },
            [&](const SettingDescription::Sub & sub) {
                json subJson;
                subJson.emplace("nullable", sub.nullable);
                subJson.emplace("map", sub.map);
                obj.emplace("subSettings", std::move(subJson));
            },
        },
        sd.info);
}

} // namespace nlohmann
