#pragma once
///@file

#include <optional>

#include "nix/util/types.hh"
#include "nix/util/abstract-setting-to-json.hh"
#include <nlohmann/json.hpp>

namespace nix {

/**
 * Save the current mount namespace. Ignored if called more than
 * once.
 */
void saveMountNamespace();

/**
 * Restore the mount namespace saved by saveMountNamespace(). Ignored
 * if saveMountNamespace() was never called.
 */
void restoreMountNamespace();

/**
 * Cause this thread to try to not share any FS attributes with the main
 * thread, because this causes setns() in restoreMountNamespace() to
 * fail.
 *
 * This is best effort -- EPERM and ENOSYS failures are just ignored.
 */
void tryUnshareFilesystem();

bool userNamespacesSupported();

bool mountAndPidNamespacesSupported();

struct SupplementaryGroup
{
public:
    std::string group;
    std::optional<gid_t> gid;
    std::string name;
    SupplementaryGroup(std::string group = "", const std::optional<gid_t> gid = std::nullopt, std::string name = "")
        : group(std::move(group))
        , gid(gid)
        , name(std::move(name))
    {
    }
    std::string to_string() const;
    bool isConflict(const SupplementaryGroup &) const;
};

typedef std::vector<SupplementaryGroup> SupplementaryGroups;

std::ostream & operator<<(std::ostream & os, const SupplementaryGroups & groups);
void to_json(nlohmann::json & j, const SupplementaryGroup & v);
void from_json(const nlohmann::json & j, SupplementaryGroup & r);

}
