#pragma once

#include "nix/util/util.hh"
#include "nix/util/json-impls.hh"
#include "nix/store/path.hh"

#include <chrono>
#include <sys/types.h>

namespace nix {

/**
 * A uid and optional corresponding user name.
 */
struct UserInfo
{
    uid_t uid = -1;
    std::optional<std::string> name;

    /**
     * Create a UserInfo from a UID, looking up the username if possible.
     */
    static UserInfo fromUid(uid_t uid);
};

struct ActiveBuild
{
    pid_t nixPid;

    std::optional<pid_t> clientPid;
    std::optional<uid_t> clientUid;

    pid_t mainPid;
    UserInfo mainUser;
    std::optional<std::filesystem::path> cgroup;

    time_t startTime;

    StorePath derivation;
};

struct ActiveBuildInfo : ActiveBuild
{
    struct ProcessInfo
    {
        pid_t pid = 0;
        pid_t parentPid = 0;
        UserInfo user;
        std::vector<std::string> argv;
        std::optional<std::chrono::microseconds> utime, stime, cutime, cstime;
    };

    // User/system CPU time for the entire cgroup, if available.
    std::optional<std::chrono::microseconds> utime, stime;

    std::vector<ProcessInfo> processes;
};

struct TrackActiveBuildsStore
{
    struct BuildHandle
    {
        TrackActiveBuildsStore & tracker;
        uint64_t id;

        BuildHandle(TrackActiveBuildsStore & tracker, uint64_t id)
            : tracker(tracker)
            , id(id)
        {
        }

        BuildHandle(BuildHandle && other) noexcept
            : tracker(other.tracker)
            , id(other.id)
        {
            other.id = 0;
        }

        ~BuildHandle()
        {
            if (id) {
                try {
                    tracker.buildFinished(*this);
                } catch (...) {
                    ignoreExceptionInDestructor();
                }
            }
        }
    };

    virtual ~TrackActiveBuildsStore();

    virtual BuildHandle buildStarted(const ActiveBuild & build) = 0;

    virtual void buildFinished(const BuildHandle & handle) = 0;
};

struct QueryActiveBuildsStore
{
    inline static std::string operationName = "Querying active builds";

    virtual ~QueryActiveBuildsStore();

    virtual std::vector<ActiveBuildInfo> queryActiveBuilds() = 0;
};

} // namespace nix

JSON_IMPL(UserInfo)
JSON_IMPL(ActiveBuild)
JSON_IMPL(ActiveBuildInfo)
JSON_IMPL(ActiveBuildInfo::ProcessInfo)
