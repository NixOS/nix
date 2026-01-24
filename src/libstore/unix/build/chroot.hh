#pragma once
///@file

#include <filesystem>
#include <map>
#include <memory>
#include <functional>

#include "nix/store/build/derivation-builder.hh"
#include "nix/util/file-system.hh"
#include "nix/store/user-lock.hh"
#include "nix/store/store-api.hh"

namespace nix {

/**
 * Parameters for setting up a chroot environment.
 */
struct BuildChrootParams
{
    /** The directory where the chroot will be created */
    std::filesystem::path chrootParentDir;

    /** Whether the derivation uses UID range feature */
    bool useUidRange;

    /** Whether the derivation type is sandboxed */
    bool isSandboxed;

    /** Build user (may be null if not using a build user) */
    UserLock * buildUser;

    /** The store directory (e.g., "/nix/store") */
    std::string storeDir;

    /** Callback to change ownership of a path to the build user */
    std::function<void(const std::filesystem::path &)> chownToBuilder;

    /** Function to get the sandbox GID */
    std::function<gid_t()> getSandboxGid;
};

/**
 * Set up a chroot build environment.
 *
 * Creates the chroot directory structure and sets up necessary directories
 * (/tmp, /etc, store directory). Returns the chroot root path and an AutoDelete
 * that will clean up the chroot directory when destroyed.
 *
 * @param params Parameters for chroot setup
 * @return Pair of (chroot root path, cleanup object)
 */
std::pair<std::filesystem::path, AutoDelete> setupBuildChroot(const BuildChrootParams & params);

} // namespace nix
