#include <vector>
#include <pwd.h>
#include <grp.h>

#include "nix/store/user-lock.hh"
#include "nix/util/file-system.hh"
#include "nix/store/globals.hh"
#include "nix/store/pathlocks.hh"
#include "nix/util/users.hh"
#include "nix/util/logging.hh"

namespace nix {

#ifdef __linux__

static std::vector<gid_t> get_group_list(const char * username, gid_t group_id)
{
    std::vector<gid_t> gids;
    gids.resize(32); // Initial guess

    auto getgroupl_failed{[&] {
        int ngroups = gids.size();
        int err = getgrouplist(username, group_id, gids.data(), &ngroups);
        gids.resize(ngroups);
        return err == -1;
    }};

    // The first error means that the vector was not big enough.
    // If it happens again, there is some different problem.
    if (getgroupl_failed() && getgroupl_failed()) {
        throw SysError("failed to get list of supplementary groups for '%s'", username);
    }

    return gids;
}
#endif

struct SimpleUserLock : UserLock
{
    AutoCloseFD fdUserLock;
    uid_t uid;
    gid_t gid;
    std::vector<gid_t> supplementaryGIDs;

    uid_t getUID() override
    {
        assert(uid);
        return uid;
    }

    uid_t getUIDCount() override
    {
        return 1;
    }

    gid_t getGID() override
    {
        assert(gid);
        return gid;
    }

    std::vector<gid_t> getSupplementaryGIDs() override
    {
        return supplementaryGIDs;
    }

    static std::unique_ptr<UserLock>
    acquire(const std::filesystem::path & userPoolDir, const std::string & buildUsersGroup)
    {
        assert(buildUsersGroup != "");

        /* Get the members of the build-users-group. */
        struct group * gr = getgrnam(buildUsersGroup.c_str());
        if (!gr)
            throw Error("the group '%s' specified in 'build-users-group' does not exist", buildUsersGroup);

        /* Copy the result of getgrnam. */
        Strings users;
        for (char ** p = gr->gr_mem; *p; ++p) {
            debug("found build user '%s'", *p);
            users.push_back(*p);
        }

        if (users.empty())
            throw Error("the build users group '%s' has no members", buildUsersGroup);

        /* Find a user account that isn't currently in use for another
           build. */
        for (auto & i : users) {
            debug("trying user '%s'", i);

            struct passwd * pw = getpwnam(i.c_str());
            if (!pw)
                throw Error("the user '%s' in the group '%s' does not exist", i, buildUsersGroup);

            auto fnUserLock = userPoolDir / std::to_string(pw->pw_uid);

            AutoCloseFD fd = open(fnUserLock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
            if (!fd)
                throw SysError("opening user lock %s", PathFmt(fnUserLock));

            if (lockFile(fd.get(), ltWrite, false)) {
                auto lock = std::make_unique<SimpleUserLock>();

                lock->fdUserLock = std::move(fd);
                lock->uid = pw->pw_uid;
                lock->gid = gr->gr_gid;

                /* Sanity check... */
                if (lock->uid == getuid() || lock->uid == geteuid())
                    throw Error("the Nix user should not be a member of '%s'", buildUsersGroup);

#ifdef __linux__
                /* Get the list of supplementary groups of this user. This is
                 * usually either empty or contains a group such as "kvm". */

                // Finally, trim back the GID list to its real size.
                for (auto gid : get_group_list(pw->pw_name, pw->pw_gid)) {
                    if (gid != lock->gid)
                        lock->supplementaryGIDs.push_back(gid);
                }
#endif

                return lock;
            }
        }

        return nullptr;
    }
};

struct AutoUserLock : UserLock
{
    AutoCloseFD fdUserLock;
    uid_t firstUid = 0;
    gid_t firstGid = 0;
    uid_t nrIds = 1;

    uid_t getUID() override
    {
        assert(firstUid);
        return firstUid;
    }

    gid_t getUIDCount() override
    {
        return nrIds;
    }

    gid_t getGID() override
    {
        assert(firstGid);
        return firstGid;
    }

    std::vector<gid_t> getSupplementaryGIDs() override
    {
        return {};
    }

    static std::unique_ptr<UserLock> acquire(
        const std::filesystem::path & userPoolDir,
        const std::string & buildUsersGroup,
        uid_t nrIds,
        bool useUserNamespace,
        const AutoAllocateUidSettings & uidSettings)
    {
#if !defined(__linux__)
        useUserNamespace = false;
#endif

        experimentalFeatureSettings.require(Xp::AutoAllocateUids);
        assert(uidSettings.startId > 0);
        assert(uidSettings.uidCount % maxIdsPerBuild == 0);
        assert((uint64_t) uidSettings.startId + (uint64_t) uidSettings.uidCount <= std::numeric_limits<uid_t>::max());
        assert(nrIds <= maxIdsPerBuild);

        size_t nrSlots = uidSettings.uidCount / maxIdsPerBuild;

        for (size_t i = 0; i < nrSlots; i++) {
            debug("trying user slot '%d'", i);

            auto fnUserLock = userPoolDir / fmt("slot-%d", i);

            AutoCloseFD fd = open(fnUserLock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
            if (!fd)
                throw SysError("opening user lock %s", PathFmt(fnUserLock));

            if (lockFile(fd.get(), ltWrite, false)) {

                auto firstUid = uidSettings.startId + i * maxIdsPerBuild;

                auto pw = getpwuid(firstUid);
                if (pw)
                    throw Error("auto-allocated UID %d clashes with existing user account '%s'", firstUid, pw->pw_name);

                auto lock = std::make_unique<AutoUserLock>();
                lock->fdUserLock = std::move(fd);
                lock->firstUid = firstUid;
                if (useUserNamespace)
                    lock->firstGid = firstUid;
                else {
                    struct group * gr = getgrnam(buildUsersGroup.c_str());
                    if (!gr)
                        throw Error("the group '%s' specified in 'build-users-group' does not exist", buildUsersGroup);
                    lock->firstGid = gr->gr_gid;
                }
                lock->nrIds = nrIds;
                return lock;
            }
        }

        return nullptr;
    }
};

std::unique_ptr<UserLock> acquireUserLock(const std::string & userGroup, uid_t nrIds, bool useUserNamespace)
{
    if (auto * uidSettings = settings.getAutoAllocateUidSettings()) {
        auto userPoolDir = settings.nixStateDir / "userpool2";
        createDirs(userPoolDir);
        return AutoUserLock::acquire(userPoolDir, userGroup, nrIds, useUserNamespace, *uidSettings);
    } else {
        auto userPoolDir = settings.nixStateDir / "userpool";
        createDirs(userPoolDir);
        return SimpleUserLock::acquire(userPoolDir, userGroup);
    }
}

bool useBuildUsers()
{
#ifdef __linux__
    static bool b = (settings.buildUsersGroup != "" || settings.autoAllocateUids) && isRootUser();
    return b;
#elif defined(__APPLE__) || defined(__FreeBSD__)
    static bool b = settings.buildUsersGroup != "" && isRootUser();
    return b;
#else
    return false;
#endif
}

} // namespace nix
