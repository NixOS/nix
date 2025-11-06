#include "nix/store/build-result.hh"
#include "nix/util/json-utils.hh"
#include <array>

namespace nix {

bool BuildResult::operator==(const BuildResult &) const noexcept = default;
std::strong_ordering BuildResult::operator<=>(const BuildResult &) const noexcept = default;

bool BuildResult::Success::operator==(const BuildResult::Success &) const noexcept = default;
std::strong_ordering BuildResult::Success::operator<=>(const BuildResult::Success &) const noexcept = default;

bool BuildResult::Failure::operator==(const BuildResult::Failure &) const noexcept = default;
std::strong_ordering BuildResult::Failure::operator<=>(const BuildResult::Failure &) const noexcept = default;

static constexpr std::array<std::pair<BuildResult::Success::Status, std::string_view>, 4> successStatusStrings{{
#define ENUM_ENTRY(e) {BuildResult::Success::e, #e}
    ENUM_ENTRY(Built),
    ENUM_ENTRY(Substituted),
    ENUM_ENTRY(AlreadyValid),
    ENUM_ENTRY(ResolvesToAlreadyValid),
#undef ENUM_ENTRY
}};

static std::string_view successStatusToString(BuildResult::Success::Status status)
{
    for (const auto & [enumVal, str] : successStatusStrings) {
        if (enumVal == status)
            return str;
    }
    throw Error("unknown success status: %d", static_cast<int>(status));
}

static BuildResult::Success::Status successStatusFromString(std::string_view str)
{
    for (const auto & [enumVal, enumStr] : successStatusStrings) {
        if (enumStr == str)
            return enumVal;
    }
    throw Error("unknown built result success status '%s'", str);
}

static constexpr std::array<std::pair<BuildResult::Failure::Status, std::string_view>, 12> failureStatusStrings{{
#define ENUM_ENTRY(e) {BuildResult::Failure::e, #e}
    ENUM_ENTRY(PermanentFailure),
    ENUM_ENTRY(InputRejected),
    ENUM_ENTRY(OutputRejected),
    ENUM_ENTRY(TransientFailure),
    ENUM_ENTRY(CachedFailure),
    ENUM_ENTRY(TimedOut),
    ENUM_ENTRY(MiscFailure),
    ENUM_ENTRY(DependencyFailed),
    ENUM_ENTRY(LogLimitExceeded),
    ENUM_ENTRY(NotDeterministic),
    ENUM_ENTRY(NoSubstituters),
    ENUM_ENTRY(HashMismatch),
#undef ENUM_ENTRY
}};

static std::string_view failureStatusToString(BuildResult::Failure::Status status)
{
    for (const auto & [enumVal, str] : failureStatusStrings) {
        if (enumVal == status)
            return str;
    }
    throw Error("unknown failure status: %d", static_cast<int>(status));
}

static BuildResult::Failure::Status failureStatusFromString(std::string_view str)
{
    for (const auto & [enumVal, enumStr] : failureStatusStrings) {
        if (enumStr == str)
            return enumVal;
    }
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
