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

/**
 * Idempotent: place this process in a private mount namespace whose
 * root is recursively MS_PRIVATE, so that subsequent mount-flag changes
 * cannot propagate back to the host. The first call performs the work;
 * later calls are cheap no-ops.
 *
 * Best-effort: if unshare() fails (e.g. restricted user namespace) the
 * caller stays in its current namespace and the function returns false.
 * Throws on the rarer case where unshare succeeds but the MS_PRIVATE
 * remount of "/" fails.
 */
bool ensurePrivateMountNamespace();

bool userNamespacesSupported();

bool mountAndPidNamespacesSupported();

} // namespace nix
