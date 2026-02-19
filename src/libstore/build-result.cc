#include "nix/store/build-result.hh"
#include "nix/util/json-utils.hh"
#include <array>

namespace nix {

unsigned int BuildError::exitCodeFromStatus(Status status)
{
    ExitStatusFlags flags{};
    flags.updateFromStatus(status);
    return flags.failingExitStatus();
}

void ExitStatusFlags::updateFromStatus(BuildResult::Failure::Status status)
{
// Allow selecting a subset of enum values
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (status) {
    case BuildResult::Failure::TimedOut:
        timedOut = true;
        break;
    case BuildResult::Failure::HashMismatch:
        hashMismatch = true;
        break;
    case BuildResult::Failure::NotDeterministic:
        checkMismatch = true;
        break;
    case BuildResult::Failure::PermanentFailure:
    // Also considered a permanent failure, it seems
    case BuildResult::Failure::InputRejected:
        permanentFailure = true;
        break;
    default:
        break;
    }
#pragma GCC diagnostic pop
}

unsigned int ExitStatusFlags::failingExitStatus() const
{
    bool buildFailure = permanentFailure || timedOut || hashMismatch;

    /* Any of the 4 booleans we track */
    bool problemWithSpecialExitCode = checkMismatch || buildFailure;

    unsigned int mask = 0;
    if (problemWithSpecialExitCode) {
        mask |= 0b1100000;
        if (buildFailure) {
            mask |= 0b0100; // 100
            if (timedOut)
                mask |= 0b0001; // 101
            if (hashMismatch)
                mask |= 0b0010; // 102
        }
        if (checkMismatch)
            mask |= 0b1000; // 104
    }

    /* We still (per the function docs) only call this function in the
       failure case, so the default should not be 0, but 1, indicating
       "some other kind of error. */
    return mask ? mask : 1;
}

bool BuildResult::operator==(const BuildResult &) const noexcept = default;
std::strong_ordering BuildResult::operator<=>(const BuildResult &) const noexcept = default;

bool BuildResult::Success::operator==(const BuildResult::Success &) const noexcept = default;
std::strong_ordering BuildResult::Success::operator<=>(const BuildResult::Success &) const noexcept = default;

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

static constexpr std::array<std::pair<BuildResult::Failure::Status, std::string_view>, 13> failureStatusStrings{{
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
    ENUM_ENTRY(Cancelled),
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

bool BuildError::operator==(const BuildError & other) const noexcept
{
    return status == other.status && isNonDeterministic == other.isNonDeterministic && message() == other.message();
}

std::strong_ordering BuildError::operator<=>(const BuildError & other) const noexcept
{
    if (auto cmp = status <=> other.status; cmp != 0)
        return cmp;
    if (auto cmp = isNonDeterministic <=> other.isNonDeterministic; cmp != 0)
        return cmp;
    return message() <=> other.message();
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
                res["errorMsg"] = failure.message();
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
        br.inner = BuildResult::Failure{{
            .status = failureStatusFromString(statusStr),
            .msg = HintFmt(getString(valueAt(json, "errorMsg"))),
            .isNonDeterministic = getBoolean(valueAt(json, "isNonDeterministic")),
        }};
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
