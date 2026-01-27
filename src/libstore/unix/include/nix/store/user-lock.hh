#pragma once
///@file

#include "nix/util/file-descriptor.hh"
#include <memory>
#include <vector>
#include <sys/types.h>

namespace nix {

struct LocalSettings;

struct UserLock
{
    virtual ~UserLock() {}

    /**
     * Get the first and last UID.
     */
    std::pair<uid_t, uid_t> getUIDRange()
    {
        auto first = getUID();
        return {first, first + getUIDCount() - 1};
    }

    /**
     * Get the first UID.
     */
    virtual uid_t getUID() = 0;

    virtual uid_t getUIDCount() = 0;

    virtual gid_t getGID() = 0;

    virtual std::vector<gid_t> getSupplementaryGIDs() = 0;

    virtual std::optional<Descriptor> getUserNamespace()
    {
        return std::nullopt;
    };

    /**
     * Get the UID that should be used inside a user namespace.
     */
    virtual uid_t getSandboxedUID()
    {
        return getUIDCount() == 1 ? 1000 : 0;
    }

    /**
     * Get the GID that should be used inside a user namespace.
     */
    virtual gid_t getSandboxedGID()
    {
        return getUIDCount() == 1 ? 100 : 0;
    }
};

/**
 * Acquire a user lock for a UID range of size `nrIds`. Note that this
 * may return nullptr if no user is available.
 */
std::unique_ptr<UserLock> acquireUserLock(const LocalSettings & localSettings, uid_t nrIds, bool useUserNamespace);

bool useBuildUsers(const LocalSettings &);

#ifdef __linux__
namespace linux {
std::unique_ptr<UserLock> acquireSystemdUserLock(uid_t nrIds);
}
#endif

} // namespace nix
