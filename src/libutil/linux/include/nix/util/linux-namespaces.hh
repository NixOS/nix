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

} // namespace nix
