#pragma once

#include "types.hh"

namespace nix {

struct UserLock
{
    virtual ~UserLock() { }

    /* Get the first and last UID. */
    virtual std::pair<uid_t, uid_t> getUIDRange() = 0;

    /* Get the first UID. */
    uid_t getUID()
    {
        return getUIDRange().first;
    }

    uid_t getUIDCount()
    {
        return getUIDRange().second - getUIDRange().first + 1;
    }

    virtual gid_t getGID() = 0;

    virtual std::vector<gid_t> getSupplementaryGIDs() = 0;

    /* Kill any processes currently executing as this user. */
    virtual void kill() = 0;

    virtual std::optional<Path> getCgroup() { return {}; };
};

/* Acquire a user lock. Note that this may return nullptr if no user
   is available. */
std::unique_ptr<UserLock> acquireUserLock();

}
