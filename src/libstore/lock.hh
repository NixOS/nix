#pragma once
///@file

#include "types.hh"
#include "acl.hh"

#include <optional>

#include <sys/types.h>

namespace nix {

struct UserLock
{
    virtual ~UserLock() { }

    /**
     * Get the first and last UID.
     */
    std::pair<uid_t, uid_t> getUIDRange() const
    {
        auto first = getUID();
        return {first, first + getUIDCount() - 1};
    }

    /**
     * Get the first UID.
     */
    virtual uid_t getUID() const = 0;

    virtual uid_t getUIDCount() const = 0;

    virtual gid_t getGID() const = 0;

    virtual std::vector<gid_t> getSupplementaryGIDs() const = 0;
};

/**
 * Acquire a user lock for a UID range of size `nrIds`. Note that this
 * may return nullptr if no user is available.
 */
std::unique_ptr<UserLock> acquireUserLock(uid_t nrIds, bool useUserNamespace);

bool useBuildUsers();

}
