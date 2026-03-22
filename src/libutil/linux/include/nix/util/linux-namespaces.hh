#pragma once
///@file

#include <filesystem>

namespace nix {

/**
 * Save the parent mount namespace and enter a private one via
 * `unshare(CLONE_NEWNS)`.
 */
void tryEnterPrivateMountNamespace();

/**
 * Remount `path` writable if its mount is read-only, leaving
 * already-writable mounts untouched. This throws if we aren't in a
 * private mount namespace, since remounting would leak into the
 * host mount table.
 */
void remountReadOnlyWritable(const std::filesystem::path & path);

/**
 * Restore the parent mount namespace saved when we entered a private
 * one. Ignored if `tryEnterPrivateMountNamespace()` never succeeded.
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

bool binfmtMiscUserNamespacesSupported();

} // namespace nix
