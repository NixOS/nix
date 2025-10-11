#include "nix/store/build-result.hh"

#include <nlohmann/json.hpp>

namespace nix {

bool BuildResult::operator==(const BuildResult &) const noexcept = default;
std::strong_ordering BuildResult::operator<=>(const BuildResult &) const noexcept = default;

bool BuildResult::Success::operator==(const BuildResult::Success &) const noexcept = default;
std::strong_ordering BuildResult::Success::operator<=>(const BuildResult::Success &) const noexcept = default;

bool BuildResult::Failure::operator==(const BuildResult::Failure &) const noexcept = default;
std::strong_ordering BuildResult::Failure::operator<=>(const BuildResult::Failure &) const noexcept = default;

void to_json(nlohmann::json & json, const BuildResult & buildResult)
{
    json = nlohmann::json::object();
    json["status"] = BuildResult::statusToString(buildResult.status());
    if (buildResult.errorMsg() != "")
        json["errorMsg"] = buildResult.errorMsg();
    if (buildResult.timesBuilt)
        json["timesBuilt"] = buildResult.timesBuilt;
    if (buildResult.startTime)
        json["startTime"] = buildResult.startTime;
    if (buildResult.stopTime)
        json["stopTime"] = buildResult.stopTime;

    auto fail = buildResult.tryGetFailure();
    if (fail != nullptr) {
        if (fail->isNonDeterministic)
            json["isNonDeterministic"] = fail->isNonDeterministic;
    }
}

void to_json(nlohmann::json & json, const KeyedBuildResult & buildResult)
{
    json = nlohmann::json((const BuildResult &) buildResult);
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
