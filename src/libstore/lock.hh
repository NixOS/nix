#pragma once

#include "types.hh"

#include <optional>

#include <sys/types.h>

namespace nix {

struct UserLock
{
    virtual ~UserLock() { }

    /* Get the first and last UID. */
    std::pair<uid_t, uid_t> getUIDRange()
    {
        auto first = getUID();
        return {first, first + getUIDCount() - 1};
    }

    /* Get the first UID. */
    virtual uid_t getUID() = 0;

    virtual uid_t getUIDCount() = 0;

    virtual gid_t getGID() = 0;

    virtual std::vector<gid_t> getSupplementaryGIDs() = 0;
};

/* Acquire a user lock for a UID range of size `nrIds`. Note that this
   may return nullptr if no user is available. */
std::unique_ptr<UserLock> acquireUserLock(uid_t nrIds, bool useChroot);

bool useBuildUsers();

}
