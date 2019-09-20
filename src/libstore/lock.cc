#include "lock.hh"
#include "globals.hh"
#include "pathlocks.hh"

#include <grp.h>
#include <pwd.h>

#include <fcntl.h>
#include <unistd.h>

namespace nix {

Sync<PathSet> UserLock::lockedPaths_;

UserLock::UserLock()
{
    assert(settings.buildUsersGroup != "");

    /* Get the members of the build-users-group. */
    struct group * gr = getgrnam(settings.buildUsersGroup.get().c_str());
    if (!gr)
        throw Error(format("the group '%1%' specified in 'build-users-group' does not exist")
            % settings.buildUsersGroup);
    gid = gr->gr_gid;

    /* Copy the result of getgrnam. */
    Strings users;
    for (char * * p = gr->gr_mem; *p; ++p) {
        debug(format("found build user '%1%'") % *p);
        users.push_back(*p);
    }

    if (users.empty())
        throw Error(format("the build users group '%1%' has no members")
            % settings.buildUsersGroup);

    /* Find a user account that isn't currently in use for another
       build. */
    for (auto & i : users) {
        debug(format("trying user '%1%'") % i);

        struct passwd * pw = getpwnam(i.c_str());
        if (!pw)
            throw Error(format("the user '%1%' in the group '%2%' does not exist")
                % i % settings.buildUsersGroup);

        createDirs(settings.nixStateDir + "/userpool");

        fnUserLock = (format("%1%/userpool/%2%") % settings.nixStateDir % pw->pw_uid).str();

        {
            auto lockedPaths(lockedPaths_.lock());
            if (lockedPaths->count(fnUserLock))
                /* We already have a lock on this one. */
                continue;
            lockedPaths->insert(fnUserLock);
        }

        try {

            AutoCloseFD fd = open(fnUserLock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
            if (!fd)
                throw SysError(format("opening user lock '%1%'") % fnUserLock);

            if (lockFile(fd.get(), ltWrite, false)) {
                fdUserLock = std::move(fd);
                user = i;
                uid = pw->pw_uid;

                /* Sanity check... */
                if (uid == getuid() || uid == geteuid())
                    throw Error(format("the Nix user should not be a member of '%1%'")
                        % settings.buildUsersGroup);

#if __linux__
                /* Get the list of supplementary groups of this build user.  This
                   is usually either empty or contains a group such as "kvm".  */
                supplementaryGIDs.resize(10);
                int ngroups = supplementaryGIDs.size();
                int err = getgrouplist(pw->pw_name, pw->pw_gid,
                    supplementaryGIDs.data(), &ngroups);
                if (err == -1)
                    throw Error(format("failed to get list of supplementary groups for '%1%'") % pw->pw_name);

                supplementaryGIDs.resize(ngroups);
#endif

                return;
            }

        } catch (...) {
            lockedPaths_.lock()->erase(fnUserLock);
        }
    }

    throw Error(format("all build users are currently in use; "
        "consider creating additional users and adding them to the '%1%' group")
        % settings.buildUsersGroup);
}


UserLock::~UserLock()
{
    auto lockedPaths(lockedPaths_.lock());
    assert(lockedPaths->count(fnUserLock));
    lockedPaths->erase(fnUserLock);
}


void UserLock::kill()
{
    killUser(uid);
}
}
