#pragma once
///@file

#include <filesystem>
#include <map>
#include <memory>
#include <functional>

namespace nix {

class AutoDelete;
struct UserLock;

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
