#include "nix/store/build-result.hh"

#include <nlohmann/json.hpp>

namespace nix {

bool BuildResult::operator==(const BuildResult &) const noexcept = default;
std::strong_ordering BuildResult::operator<=>(const BuildResult &) const noexcept = default;

void to_json(nlohmann::json & json, const BuildResult & buildResult)
{
    json = nlohmann::json::object();
    json["status"] = BuildResult::statusToString(buildResult.status);
    if (buildResult.errorMsg != "")
        json["errorMsg"] = buildResult.errorMsg;
    if (buildResult.timesBuilt)
        json["timesBuilt"] = buildResult.timesBuilt;
    if (buildResult.isNonDeterministic)
        json["isNonDeterministic"] = buildResult.isNonDeterministic;
    if (buildResult.startTime)
        json["startTime"] = buildResult.startTime;
    if (buildResult.stopTime)
        json["stopTime"] = buildResult.stopTime;
}

void to_json(nlohmann::json & json, const KeyedBuildResult & buildResult)
{
    to_json(json, (const BuildResult &) buildResult);
    auto path = nlohmann::json::object();
    std::visit(
        overloaded{
            [&](const DerivedPathOpaque & opaque) { path["opaque"] = opaque.path.to_string(); },
            [&](const DerivedPathBuilt & drv) {
                path["drvPath"] = drv.drvPath->getBaseStorePath().to_string();
                path["outputs"] = drv.outputs.to_string();
            },
        },
        buildResult.path.raw());
    json["path"] = std::move(path);
}

} // namespace nix
