#include "build-result.hh"

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

nlohmann::json KeyedBuildResult::toJSON(Store & store) const
{
    auto json = nlohmann::json((const BuildResult &) *this);
    json["path"] = path.toJSON(store);
    return json;
}

}
