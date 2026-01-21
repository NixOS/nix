#pragma once
/**
 * @file
 *
 * Template implementations for cache.hh.
 *
 * Include this file when you need to use withFetchLock().
 */

#include "nix/fetchers/cache.hh"
#include "nix/store/pathlocks.hh"
#include "nix/util/finally.hh"
#include "nix/util/logging.hh"

namespace nix::fetchers {

template<typename CheckCache, typename DoFetch>
auto withFetchLock(
    std::string_view lockIdentity, unsigned int lockTimeout, CheckCache && checkCache, DoFetch && doFetch)
    -> decltype(doFetch())
{
    auto lockPath = getFetchLockPath(lockIdentity);

    /* Acquire exclusive lock with stale detection. */
    AutoCloseFD fd = acquireExclusiveFileLock(lockPath, lockTimeout, lockIdentity);

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

    /* Double-check: another process may have populated the cache
       while we were waiting for the lock. */
    if (auto cached = checkCache()) {
        return *cached;
    }

    /* Perform the actual fetch. */
    return doFetch();
}

} // namespace nix::fetchers
