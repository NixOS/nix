#include "lock.hh"
#include "globals.hh"
#include "pathlocks.hh"

#include <pwd.h>
#include <grp.h>

namespace nix {

struct SimpleUserLock : UserLock
{
    AutoCloseFD fdUserLock;
    uid_t uid;
    gid_t gid;
    std::vector<gid_t> supplementaryGIDs;

    uid_t getUID() override { assert(uid); return uid; }
    uid_t getUIDCount() override { return 1; }
    gid_t getGID() override { assert(gid); return gid; }

    std::vector<gid_t> getSupplementaryGIDs() override { return supplementaryGIDs; }

    static std::unique_ptr<UserLock> acquire()
    {
        assert(settings.buildUsersGroup != "");
        createDirs(settings.nixStateDir + "/userpool");

        /* Get the members of the build-users-group. */
        struct group * gr = getgrnam(settings.buildUsersGroup.get().c_str());
        if (!gr)
            throw Error("the group '%s' specified in 'build-users-group' does not exist", settings.buildUsersGroup);

        /* Copy the result of getgrnam. */
        Strings users;
        for (char * * p = gr->gr_mem; *p; ++p) {
            debug("found build user '%s'", *p);
            users.push_back(*p);
        }

        if (users.empty())
            throw Error("the build users group '%s' has no members", settings.buildUsersGroup);

        /* Find a user account that isn't currently in use for another
           build. */
        for (auto & i : users) {
            debug("trying user '%s'", i);

            struct passwd * pw = getpwnam(i.c_str());
            if (!pw)
                throw Error("the user '%s' in the group '%s' does not exist", i, settings.buildUsersGroup);

            auto fnUserLock = fmt("%s/userpool/%s", settings.nixStateDir,pw->pw_uid);

            AutoCloseFD fd = open(fnUserLock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
            if (!fd)
                throw SysError("opening user lock '%s'", fnUserLock);

            if (lockFile(fd.get(), ltWrite, false)) {
                auto lock = std::make_unique<SimpleUserLock>();

                lock->fdUserLock = std::move(fd);
                lock->uid = pw->pw_uid;
                lock->gid = gr->gr_gid;

                /* Sanity check... */
                if (lock->uid == getuid() || lock->uid == geteuid())
                    throw Error("the Nix user should not be a member of '%s'", settings.buildUsersGroup);

                #if __linux__
                /* Get the list of supplementary groups of this build
                   user.  This is usually either empty or contains a
                   group such as "kvm".  */
                int ngroups = 32; // arbitrary initial guess
                std::vector<gid_t> gids;
                gids.resize(ngroups);

                int err = getgrouplist(
                    pw->pw_name, pw->pw_gid,
                    gids.data(),
                    &ngroups);

                /* Our initial size of 32 wasn't sufficient, the
                   correct size has been stored in ngroups, so we try
                   again. */
                if (err == -1) {
                    gids.resize(ngroups);
                    err = getgrouplist(
                        pw->pw_name, pw->pw_gid,
                        gids.data(),
                        &ngroups);
                }

                // If it failed once more, then something must be broken.
                if (err == -1)
                    throw Error("failed to get list of supplementary groups for '%s'", pw->pw_name);

                // Finally, trim back the GID list to its real size.
                for (auto i = 0; i < ngroups; i++)
                    if (gids[i] != lock->gid)
                        lock->supplementaryGIDs.push_back(gids[i]);
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

    uid_t getUID() override { assert(firstUid); return firstUid; }

    gid_t getUIDCount() override { return nrIds; }

    gid_t getGID() override { assert(firstGid); return firstGid; }

    std::vector<gid_t> getSupplementaryGIDs() override { return {}; }

    static std::unique_ptr<UserLock> acquire(uid_t nrIds, bool useChroot)
    {
        settings.requireExperimentalFeature(Xp::AutoAllocateUids);
        assert(settings.startId > 0);
        assert(settings.uidCount % maxIdsPerBuild == 0);
        assert((uint64_t) settings.startId + (uint64_t) settings.uidCount <= std::numeric_limits<uid_t>::max());
        assert(nrIds <= maxIdsPerBuild);

        createDirs(settings.nixStateDir + "/userpool2");

        size_t nrSlots = settings.uidCount / maxIdsPerBuild;

        for (size_t i = 0; i < nrSlots; i++) {
            debug("trying user slot '%d'", i);

            createDirs(settings.nixStateDir + "/userpool2");

            auto fnUserLock = fmt("%s/userpool2/slot-%d", settings.nixStateDir, i);

            AutoCloseFD fd = open(fnUserLock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
            if (!fd)
                throw SysError("opening user lock '%s'", fnUserLock);

            if (lockFile(fd.get(), ltWrite, false)) {

                auto firstUid = settings.startId + i * maxIdsPerBuild;

                auto pw = getpwuid(firstUid);
                if (pw)
                    throw Error("auto-allocated UID %d clashes with existing user account '%s'", firstUid, pw->pw_name);

                auto lock = std::make_unique<AutoUserLock>();
                lock->fdUserLock = std::move(fd);
                lock->firstUid = firstUid;
                if (useChroot)
                    lock->firstGid = firstUid;
                else {
                    struct group * gr = getgrnam(settings.buildUsersGroup.get().c_str());
                    if (!gr)
                        throw Error("the group '%s' specified in 'build-users-group' does not exist", settings.buildUsersGroup);
                    lock->firstGid = gr->gr_gid;
                }
                lock->nrIds = nrIds;
                return lock;
            }
        }

        return nullptr;
    }
};

std::unique_ptr<UserLock> acquireUserLock(uid_t nrIds, bool useChroot)
{
    if (settings.autoAllocateUids)
        return AutoUserLock::acquire(nrIds, useChroot);
    else
        return SimpleUserLock::acquire();
}

bool useBuildUsers()
{
    #if __linux__
    static bool b = (settings.buildUsersGroup != "" || settings.startId.get() != 0) && getuid() == 0;
    return b;
    #elif __APPLE__
    static bool b = settings.buildUsersGroup != "" && getuid() == 0;
    return b;
    #else
    return false;
    #endif
}

}
