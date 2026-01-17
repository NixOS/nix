#pragma once
/**
 * @file
 *
 * Template implementations for substitution-lock.hh.
 *
 * Include this file when you need to use withSubstitutionLock().
 */

#include "nix/store/substitution-lock.hh"
#include "nix/store/pathlocks.hh"
#include "nix/util/finally.hh"
#include "nix/util/logging.hh"

namespace nix {

template<typename CheckExists, typename DoCopy>
void withSubstitutionLock(
    std::string_view hashPart, unsigned int lockTimeout, CheckExists && checkExists, DoCopy && doCopy)
{
    auto lockPath = getSubstitutionLockPath(hashPart);

    /* Acquire exclusive lock with stale detection. */
    AutoCloseFD fd = acquireExclusiveFileLock(lockPath, lockTimeout, hashPart);

    /* Ensure lock file is cleaned up on all exit paths, including exceptions.
       The flock is automatically released when fd closes, but we also want
       to remove the lock file from disk. Errors during cleanup are ignored
       since lock file removal is an optimization, not a necessity. */
    Finally cleanup([&]() {
        try {
            deleteLockFile(lockPath, fd.get());
        } catch (...) {
            /* Ignore errors during cleanup - the flock is released when fd closes */
        }
    });

    /* Double-check: another process may have completed the substitution
       while we were waiting for the lock. */
    if (checkExists()) {
        debug("store path already valid after acquiring lock, skipping copy");
        return;
    }

    /* Perform the actual copy. */
    doCopy();
}

} // namespace nix
