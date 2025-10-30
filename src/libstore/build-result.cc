#include "nix/store/build-result.hh"
#include "nix/util/json-utils.hh"

namespace nix {

bool BuildResult::operator==(const BuildResult &) const noexcept = default;
std::strong_ordering BuildResult::operator<=>(const BuildResult &) const noexcept = default;

bool BuildResult::Success::operator==(const BuildResult::Success &) const noexcept = default;
std::strong_ordering BuildResult::Success::operator<=>(const BuildResult::Success &) const noexcept = default;

bool BuildResult::Failure::operator==(const BuildResult::Failure &) const noexcept = default;
std::strong_ordering BuildResult::Failure::operator<=>(const BuildResult::Failure &) const noexcept = default;

static std::string_view successStatusToString(BuildResult::Success::Status status)
{
    switch (status) {
    case BuildResult::Success::Built:
        return "built";
    case BuildResult::Success::Substituted:
        return "substituted";
    case BuildResult::Success::AlreadyValid:
        return "already valid";
    case BuildResult::Success::ResolvesToAlreadyValid:
        return "resolves to already valid";
    }
}

static BuildResult::Success::Status successStatusFromString(std::string_view str)
{
    if (str == "built")
        return BuildResult::Success::Built;
    if (str == "substituted")
        return BuildResult::Success::Substituted;
    if (str == "already valid")
        return BuildResult::Success::AlreadyValid;
    if (str == "resolves to already valid")
        return BuildResult::Success::ResolvesToAlreadyValid;
    throw Error("unknown built result success status '%s'", str);
}

static std::string_view failureStatusToString(BuildResult::Failure::Status status)
{
    switch (status) {
    case BuildResult::Failure::PermanentFailure:
        return "permanent failure";
    case BuildResult::Failure::InputRejected:
        return "input rejected";
    case BuildResult::Failure::OutputRejected:
        return "output rejected";
    case BuildResult::Failure::TransientFailure:
        return "transient failure";
    case BuildResult::Failure::CachedFailure:
        return "cached failure";
    case BuildResult::Failure::TimedOut:
        return "timed out";
    case BuildResult::Failure::MiscFailure:
        return "misc failure";
    case BuildResult::Failure::DependencyFailed:
        return "dependency failed";
    case BuildResult::Failure::LogLimitExceeded:
        return "log limit exceeded";
    case BuildResult::Failure::NotDeterministic:
        return "not deterministic";
    case BuildResult::Failure::NoSubstituters:
        return "no substituters";
    case BuildResult::Failure::HashMismatch:
        return "hash mismatch";
    }
}

static BuildResult::Failure::Status failureStatusFromString(std::string_view str)
{
    if (str == "permanent failure")
        return BuildResult::Failure::PermanentFailure;
    if (str == "input rejected")
        return BuildResult::Failure::InputRejected;
    if (str == "output rejected")
        return BuildResult::Failure::OutputRejected;
    if (str == "transient failure")
        return BuildResult::Failure::TransientFailure;
    if (str == "cached failure")
        return BuildResult::Failure::CachedFailure;
    if (str == "timed out")
        return BuildResult::Failure::TimedOut;
    if (str == "misc failure")
        return BuildResult::Failure::MiscFailure;
    if (str == "dependency failed")
        return BuildResult::Failure::DependencyFailed;
    if (str == "log limit exceeded")
        return BuildResult::Failure::LogLimitExceeded;
    if (str == "not deterministic")
        return BuildResult::Failure::NotDeterministic;
    if (str == "no substituters")
        return BuildResult::Failure::NoSubstituters;
    if (str == "hash mismatch")
        return BuildResult::Failure::HashMismatch;
    throw Error("unknown built result failure status '%s'", str);
}

} // namespace nix

namespace nlohmann {

using namespace nix;

void adl_serializer<BuildResult>::to_json(json & res, const BuildResult & br)
{
    res = json::object();

    // Common fields
    res["timesBuilt"] = br.timesBuilt;
    res["startTime"] = br.startTime;
    res["stopTime"] = br.stopTime;

    if (br.cpuUser.has_value()) {
        res["cpuUser"] = br.cpuUser->count();
    }
    if (br.cpuSystem.has_value()) {
        res["cpuSystem"] = br.cpuSystem->count();
    }

    // Handle success or failure variant
    std::visit(
        overloaded{
            [&](const BuildResult::Success & success) {
                res["success"] = true;
                res["status"] = successStatusToString(success.status);
                res["builtOutputs"] = success.builtOutputs;
            },
            [&](const BuildResult::Failure & failure) {
                res["success"] = false;
                res["status"] = failureStatusToString(failure.status);
                res["errorMsg"] = failure.errorMsg;
                res["isNonDeterministic"] = failure.isNonDeterministic;
            },
        },
        br.inner);
}

BuildResult adl_serializer<BuildResult>::from_json(const json & _json)
{
    auto & json = getObject(_json);

    BuildResult br;

    // Common fields
    br.timesBuilt = getUnsigned(valueAt(json, "timesBuilt"));
    br.startTime = getUnsigned(valueAt(json, "startTime"));
    br.stopTime = getUnsigned(valueAt(json, "stopTime"));

    if (auto cpuUser = optionalValueAt(json, "cpuUser")) {
        br.cpuUser = std::chrono::microseconds(getUnsigned(*cpuUser));
    }
    if (auto cpuSystem = optionalValueAt(json, "cpuSystem")) {
        br.cpuSystem = std::chrono::microseconds(getUnsigned(*cpuSystem));
    }

    // Determine success or failure based on success field
    bool success = getBoolean(valueAt(json, "success"));
    std::string statusStr = getString(valueAt(json, "status"));

    if (success) {
        BuildResult::Success s;
        s.status = successStatusFromString(statusStr);
        s.builtOutputs = valueAt(json, "builtOutputs");
        br.inner = std::move(s);
    } else {
        BuildResult::Failure f;
        f.status = failureStatusFromString(statusStr);
        f.errorMsg = getString(valueAt(json, "errorMsg"));
        f.isNonDeterministic = getBoolean(valueAt(json, "isNonDeterministic"));
        br.inner = std::move(f);
    }

    return br;
}

KeyedBuildResult adl_serializer<KeyedBuildResult>::from_json(const json & json0)
{
    auto json = getObject(json0);

    return KeyedBuildResult{
        adl_serializer<BuildResult>::from_json(json0),
        valueAt(json, "path"),
    };
}

void adl_serializer<KeyedBuildResult>::to_json(json & json, const KeyedBuildResult & kbr)
{
    adl_serializer<BuildResult>::to_json(json, kbr);
    json["path"] = kbr.path;
}

} // namespace nlohmann
