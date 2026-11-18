#pragma once
///@file

#include <filesystem>
#include <map>
#include <memory>
#include <functional>

#include "nix/util/file-descriptor.hh"

namespace nix {

class AutoDeleteAt;
struct UserLock;

/**
 * Parameters for setting up a chroot environment.
 */
struct BuildChrootParams
{
    /** The directory where the chroot will be created */
    std::filesystem::path chrootParentDir;

    /**
     * A handle to the real store directory, which `chrootParentDir` is
     * directly in.
     */
    Descriptor realStoreDirFd;

    /** Whether the derivation uses UID range feature */
    bool useUidRange;

    /** Whether the derivation type is sandboxed */
    bool isSandboxed;

    /** Build user (may be null if not using a build user) */
    UserLock * buildUser;

    /** The store directory (e.g., "/nix/store") */
    std::string storeDir;

    /**
     * Callback to change ownership of an open file to the build user.
     * The path is only for error messages.
     */
    std::function<void(Descriptor fd, const std::filesystem::path & path)> chownToBuilder;
};

/**
 * Set up a chroot build environment.
 *
 * Creates the chroot directory structure and sets up necessary directories
 * (/tmp, /etc, store directory). Returns the chroot root path and an `AutoDeleteAt`
 * that will clean up the chroot directory when destroyed.
 *
 * @param params Parameters for chroot setup
 * @return Pair of (chroot root path, cleanup object)
 */
std::pair<std::filesystem::path, AutoDeleteAt> setupBuildChroot(const BuildChrootParams & params);

} // namespace nix
