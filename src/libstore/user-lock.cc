#include "user-lock.hh"
#include "globals.hh"
#include "pathlocks.hh"
#include "cgroup.hh"

namespace nix {

struct SimpleUserLock : UserLock
{
    AutoCloseFD fdUserLock;
    uid_t uid;
    gid_t gid;
    std::vector<gid_t> supplementaryGIDs;

    void kill() override
    {
        killUser(uid);
    }

    std::pair<uid_t, uid_t> getUIDRange() override
    {
        assert(uid);
        return {uid, uid};
    }

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
                lock->supplementaryGIDs.resize(10);
                int ngroups = lock->supplementaryGIDs.size();
                int err = getgrouplist(pw->pw_name, pw->pw_gid,
                    lock->supplementaryGIDs.data(), &ngroups);
                if (err == -1)
                    throw Error("failed to get list of supplementary groups for '%s'", pw->pw_name);

                lock->supplementaryGIDs.resize(ngroups);
                #endif

                return lock;
            }
        }

        return nullptr;
    }
};

#if __linux__
struct CgroupUserLock : UserLock
{
    AutoCloseFD fdUserLock;
    uid_t uid;

    void kill() override
    {
        if (cgroup) {
            destroyCgroup(*cgroup);
            cgroup.reset();
        }
    }

    std::pair<uid_t, uid_t> getUIDRange() override
    {
        assert(uid);
        return {uid, uid + settings.idsPerBuild - 1};
    }

    gid_t getGID() override
    {
        // We use the same GID ranges as for the UIDs.
        assert(uid);
        return uid;
    }

    std::vector<gid_t> getSupplementaryGIDs() override { return {}; }

    static std::unique_ptr<UserLock> acquire()
    {
        settings.requireExperimentalFeature("auto-allocate-uids");
        assert(settings.startId > 0);
        assert(settings.startId % settings.idsPerBuild == 0);
        assert(settings.uidCount % settings.idsPerBuild == 0);
        assert((uint64_t) settings.startId + (uint64_t) settings.uidCount <= std::numeric_limits<uid_t>::max());

        // FIXME: check whether the id range overlaps any known users

        createDirs(settings.nixStateDir + "/userpool2");

        size_t nrSlots = settings.uidCount / settings.idsPerBuild;

        for (size_t i = 0; i < nrSlots; i++) {
            debug("trying user slot '%d'", i);

            createDirs(settings.nixStateDir + "/userpool2");

            auto fnUserLock = fmt("%s/userpool2/slot-%d", settings.nixStateDir, i);

            AutoCloseFD fd = open(fnUserLock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
            if (!fd)
                throw SysError("opening user lock '%s'", fnUserLock);

            if (lockFile(fd.get(), ltWrite, false)) {
                auto lock = std::make_unique<CgroupUserLock>();
                lock->fdUserLock = std::move(fd);
                lock->uid = settings.startId + i * settings.idsPerBuild;
                auto s = drainFD(lock->fdUserLock.get());
                if (s != "") lock->cgroup = s;
                return lock;
            }
        }

        return nullptr;
    }

    std::optional<Path> cgroup;

    std::optional<Path> getCgroup() override
    {
        if (!cgroup) {
            /* Create a systemd cgroup since that's the minimum
               required by systemd-nspawn. */
            auto ourCgroups = getCgroups("/proc/self/cgroup");
            auto systemdCgroup = ourCgroups["systemd"];
            if (systemdCgroup == "")
                throw Error("'systemd' cgroup does not exist");

            auto hostCgroup = canonPath("/sys/fs/cgroup/systemd/" + systemdCgroup);

            if (!pathExists(hostCgroup))
                throw Error("expected cgroup directory '%s'", hostCgroup);

            cgroup = fmt("%s/nix-%d", hostCgroup, uid);

            destroyCgroup(*cgroup);

            if (mkdir(cgroup->c_str(), 0755) == -1)
                throw SysError("creating cgroup '%s'", *cgroup);

            /* Record the cgroup in the lock file. This ensures that
               if we subsequently get executed under a different parent
               cgroup, we kill the previous cgroup first. */
            if (ftruncate(fdUserLock.get(), 0) == -1)
                throw Error("truncating user lock");
            writeFull(fdUserLock.get(), *cgroup);
        }

        return cgroup;
    };
};
#endif

std::unique_ptr<UserLock> acquireUserLock()
{
    #if __linux__
    if (settings.autoAllocateUids)
        return CgroupUserLock::acquire();
    else
    #endif
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
