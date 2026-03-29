#pragma once
///@file

#include <optional>

#include "nix/util/types.hh"

namespace nix {

/**
 * Save the current mount namespace. Ignored if called more than
 * once.
 */
void saveMountNamespace();

/**
 * Whether we successfully entered a private mount namespace via
 * `unshare(CLONE_NEWNS)`. When false, remounting the store writable
 * would affect the host mount table.
 */
bool havePrivateMountNamespace();

/**
 * Record that we have entered a private mount namespace.
 */
void setHavePrivateMountNamespace();

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

} // namespace nix
