#include "nix/store/active-builds.hh"
#include "nix/util/json-utils.hh"

#include <nlohmann/json.hpp>

#ifndef _WIN32
#  include <pwd.h>
#endif

namespace nix {

TrackActiveBuildsStore::~TrackActiveBuildsStore() = default;
QueryActiveBuildsStore::~QueryActiveBuildsStore() = default;

UserInfo UserInfo::fromUid(uid_t uid)
{
    UserInfo info;
    info.uid = uid;

#ifndef _WIN32
    // Look up the user name for the UID (thread-safe)
    struct passwd pwd;
    struct passwd * result;
    std::vector<char> buf(16384);
    if (getpwuid_r(uid, &pwd, buf.data(), buf.size(), &result) == 0 && result)
        info.name = result->pw_name;
#endif

    return info;
}

} // namespace nix

namespace nlohmann {

using namespace nix;

UserInfo adl_serializer<UserInfo>::from_json(const json & j)
{
    return UserInfo{
        .uid = j.at("uid").get<uid_t>(),
        .name = j.contains("name") && !j.at("name").is_null()
                    ? std::optional<std::string>(j.at("name").get<std::string>())
                    : std::nullopt,
    };
}

void adl_serializer<UserInfo>::to_json(json & j, const UserInfo & info)
{
    j = nlohmann::json{
        {"uid", info.uid},
        {"name", info.name},
    };
}

// Durations are serialized as floats representing seconds.
static std::optional<std::chrono::microseconds> parseDuration(const json & j, const char * key)
{
    if (j.contains(key) && !j.at(key).is_null())
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::duration<float, std::chrono::seconds::period>(j.at(key).get<double>()));
    else
        return std::nullopt;
}

static nlohmann::json printDuration(const std::optional<std::chrono::microseconds> & duration)
{
    return duration
               ? nlohmann::json(
                     std::chrono::duration_cast<std::chrono::duration<float, std::chrono::seconds::period>>(*duration)
                         .count())
               : nullptr;
}

ActiveBuildInfo::ProcessInfo adl_serializer<ActiveBuildInfo::ProcessInfo>::from_json(const json & j)
{
    return ActiveBuildInfo::ProcessInfo{
        .pid = j.at("pid").get<pid_t>(),
        .parentPid = j.at("parentPid").get<pid_t>(),
        .user = j.at("user").get<UserInfo>(),
        .argv = j.at("argv").get<std::vector<std::string>>(),
        .utime = parseDuration(j, "utime"),
        .stime = parseDuration(j, "stime"),
        .cutime = parseDuration(j, "cutime"),
        .cstime = parseDuration(j, "cstime"),
        .mem = j.at("mem").get<unsigned>(),
    };
}

void adl_serializer<ActiveBuildInfo::ProcessInfo>::to_json(json & j, const ActiveBuildInfo::ProcessInfo & process)
{
    j = nlohmann::json{
        {"pid", process.pid},
        {"parentPid", process.parentPid},
        {"user", process.user},
        {"argv", process.argv},
        {"utime", printDuration(process.utime)},
        {"stime", printDuration(process.stime)},
        {"cutime", printDuration(process.cutime)},
        {"cstime", printDuration(process.cstime)},
        {"mem", process.mem.value_or(0)},
    };
}

ActiveBuild adl_serializer<ActiveBuild>::from_json(const json & j)
{
    auto type = j.at("type").get<std::string>();
    if (type != "build")
        throw Error("invalid active build JSON: expected type 'build' but got '%s'", type);
    std::optional<std::filesystem::path> cgroup;
    if (!j.at("cgroup").is_null())
        cgroup = j.at("cgroup").get<std::filesystem::path>();
    return ActiveBuild{
        .nixPid = j.at("nixPid").get<pid_t>(),
        .clientPid = j.at("clientPid").get<std::optional<pid_t>>(),
        .clientUid = j.at("clientUid").get<std::optional<uid_t>>(),
        .mainPid = j.at("mainPid").get<pid_t>(),
        .mainUser = j.at("mainUser").get<UserInfo>(),
        .cgroup = std::move(cgroup),
        .startTime = (time_t) j.at("startTime").get<double>(),
        .derivation = StorePath{getString(j.at("derivation"))},
    };
}

void adl_serializer<ActiveBuild>::to_json(json & j, const ActiveBuild & build)
{
    j = nlohmann::json{
        {"type", "build"},
        {"nixPid", build.nixPid},
        {"clientPid", build.clientPid},
        {"clientUid", build.clientUid},
        {"mainPid", build.mainPid},
        {"mainUser", build.mainUser},
        {"cgroup", build.cgroup ? nlohmann::json(*build.cgroup) : nlohmann::json(nullptr)},
        {"startTime", (double) build.startTime},
        {"derivation", build.derivation.to_string()},
    };
}

ActiveBuildInfo adl_serializer<ActiveBuildInfo>::from_json(const json & j)
{
    ActiveBuildInfo info(adl_serializer<ActiveBuild>::from_json(j));
    info.processes = j.at("processes").get<std::vector<ActiveBuildInfo::ProcessInfo>>();
    info.utime = parseDuration(j, "utime");
    info.stime = parseDuration(j, "stime");
    info.mem = j.at("mem").get<unsigned>();
    return info;
}

void adl_serializer<ActiveBuildInfo>::to_json(json & j, const ActiveBuildInfo & build)
{
    adl_serializer<ActiveBuild>::to_json(j, build);
    j["processes"] = build.processes;
    j["utime"] = printDuration(build.utime);
    j["stime"] = printDuration(build.stime);
    j["mem"] = build.mem.value_or(0);
}

} // namespace nlohmann
