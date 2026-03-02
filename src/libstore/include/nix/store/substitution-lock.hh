#pragma once
/**
 * @file
 *
 * Process-safe locking for store path substitutions.
 *
 * This module provides file-based locking to prevent multiple processes
 * from downloading the same store path simultaneously from a binary cache.
 */

#include "nix/util/types.hh"

#include <string_view>

namespace nix {

/**
 * Get the path for a substitution lock file based on a store path hash.
 * The hash is used to create a unique lock file name in
 * ~/.cache/nix/substitution-locks/.
 *
 * @param hashPart The hash part of a store path (e.g., "abc123...")
 * @return Path to the lock file
 */
Path getSubstitutionLockPath(std::string_view hashPart);

/**
 * Execute a function while holding a substitution lock.
 * Implements double-checked locking with stale lock detection.
 *
 * This helper coordinates between processes to prevent duplicate downloads.
 * It acquires a file lock, checks if the path is already valid, and only
 * performs the copy if necessary.
 *
 * @tparam CheckExists Callable returning bool (true if path exists, skip copy)
 * @tparam DoCopy Callable performing the actual copy operation
 * @param hashPart Store path hash part (used to generate lock path)
 * @param lockTimeout Timeout in seconds (0 = wait indefinitely)
 * @param checkExists Called after acquiring lock (double-check); if returns true, skip copy
 * @param doCopy Called under lock if checkExists returns false
 * @throws Error if lock acquisition times out
 */
template<typename CheckExists, typename DoCopy>
void withSubstitutionLock(
    std::string_view hashPart, unsigned int lockTimeout, CheckExists && checkExists, DoCopy && doCopy);

} // namespace nix
