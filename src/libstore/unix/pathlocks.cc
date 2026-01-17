#include "nix/store/pathlocks.hh"
#include "nix/util/util.hh"
#include "nix/util/sync.hh"
#include "nix/util/signals.hh"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <thread>

using namespace std::chrono_literals;

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>

namespace nix {

AutoCloseFD openLockFile(const std::filesystem::path & path, bool create)
{
    AutoCloseFD fd;

    fd = open(path.c_str(), O_CLOEXEC | O_RDWR | (create ? O_CREAT : 0), 0600);
    if (!fd && (create || errno != ENOENT))
        throw SysError("opening lock file %1%", path);

    return fd;
}

void deleteLockFile(const std::filesystem::path & path, Descriptor desc)
{
    /* Get rid of the lock file.  Have to be careful not to introduce
       races.  Write a (meaningless) token to the file to indicate to
       other processes waiting on this lock that the lock is stale
       (deleted).

       IMPORTANT: Only write the stale marker if unlink succeeds.
       If unlink fails but writeFull succeeds, the file would be
       permanently poisoned with the stale marker, causing a livelock
       where all processes retry forever. If unlink succeeds but
       writeFull fails, waiters can still detect staleness via
       st_nlink == 0 (the file descriptor points to an unlinked file). */
    if (unlink(path.c_str()) == 0) {
        try {
            writeFull(desc, "d");
        } catch (...) {
            /* Ignore - file is unlinked, waiters detect via st_nlink */
        }
    }
    /* Note: if unlink fails, we don't write the marker. The lock file
       remains usable for future locking attempts. */
}

bool lockFile(Descriptor desc, LockType lockType, bool wait)
{
    int type;
    if (lockType == ltRead)
        type = LOCK_SH;
    else if (lockType == ltWrite)
        type = LOCK_EX;
    else if (lockType == ltNone)
        type = LOCK_UN;
    else
        unreachable();

    if (wait) {
        while (flock(desc, type) != 0) {
            checkInterrupt();
            if (errno != EINTR)
                throw SysError("acquiring/releasing lock");
            /* If EINTR, the loop continues and retries flock() */
        }
    } else {
        while (flock(desc, type | LOCK_NB) != 0) {
            checkInterrupt();
            if (errno == EWOULDBLOCK)
                return false;
            if (errno != EINTR)
                throw SysError("acquiring/releasing lock");
        }
    }

    return true;
}

bool lockFileWithTimeout(Descriptor desc, LockType lockType, unsigned int timeout)
{
    if (timeout == 0) {
        /* No timeout - wait indefinitely */
        return lockFile(desc, lockType, true);
    }

    /*
     * flock() doesn't support timeouts natively. We use a polling approach
     * with exponential backoff, which is the standard solution for timed
     * flock() since:
     *
     * 1. Using alarm()/SIGALRM is not thread-safe and interferes with
     *    other signal handling.
     *
     * 2. poll()/select() don't work with flock() - you can't wait on
     *    lock acquisition in a select-like manner.
     *
     * 3. Boost Interprocess file_lock uses a different locking mechanism
     *    (fcntl F_SETLK) that's incompatible with flock(), so mixing them
     *    would cause deadlocks with other Nix processes.
     *
     * The exponential backoff (10ms -> 20ms -> ... -> 500ms cap) minimizes
     * CPU usage while remaining responsive to lock availability.
     */
    auto startTime = std::chrono::steady_clock::now();
    auto timeoutDuration = std::chrono::seconds(timeout);
    auto sleepDuration = 10ms;
    constexpr auto maxSleep = 500ms;

    while (true) {
        checkInterrupt();

        /* Try non-blocking lock */
        if (lockFile(desc, lockType, false))
            return true;

        /* Check if we've exceeded the timeout */
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        auto remaining = timeoutDuration - elapsed;
        if (remaining <= std::chrono::milliseconds(0))
            return false;

        /* Sleep for min(sleepDuration, remaining) to respect timeout precisely.
           This ensures we don't overshoot the timeout by up to maxSleep.
           Guard against busy-spin when remaining < 1ms (duration_cast truncates to 0). */
        auto actualSleep = std::min(sleepDuration, std::chrono::duration_cast<std::chrono::milliseconds>(remaining));
        if (actualSleep <= std::chrono::milliseconds(0))
            return false;
        std::this_thread::sleep_for(actualSleep);
        sleepDuration = std::min(sleepDuration * 2, maxSleep);
    }
}

AutoCloseFD
acquireExclusiveFileLock(const std::filesystem::path & lockPath, unsigned int timeout, std::string_view identity)
{
    debug("acquiring lock '%s' for '%s'", lockPath.string(), identity);

    AutoCloseFD fd;

    /* Loop to handle stale lock files. A lock file becomes stale when
       another process deletes it while we're waiting to acquire it.
       We detect this by checking if the file has content (deleteLockFile
       writes a marker byte before unlinking). */
    while (true) {
        /* Open/create the lock file. */
        fd = openLockFile(lockPath, true);
        if (!fd)
            throw Error("failed to open lock file '%s'", lockPath.string());

        /* Try to acquire the lock without blocking first. */
        if (!lockFile(fd.get(), ltWrite, false)) {
            /* Lock is contested - log that we're waiting, then block. */
            if (timeout > 0) {
                printInfo("waiting for lock on '%s' (timeout: %us)...", identity, timeout);
            } else {
                printInfo("waiting for lock on '%s'...", identity);
            }

            if (!lockFileWithTimeout(fd.get(), ltWrite, timeout)) {
                throw Error("timed out waiting for lock on '%s' after %u seconds", identity, timeout);
            }
        }

        debug("lock acquired on '%s'", lockPath.string());

        /* Check if the lock file has become stale.
           Three conditions indicate staleness:
           1. st_size != 0: The previous holder wrote a stale marker
           2. st_nlink == 0: The file was unlinked (crash or marker write failed)
           3. inode mismatch: A new file was created at the same path */
        struct stat st;
        if (fstat(fd.get(), &st) == -1)
            throw SysError("statting lock file '%s'", lockPath.string());

        /* Check 1: Stale marker written by previous holder.
           If the file still exists on disk (nlink > 0), delete it so the
           next iteration creates a fresh file. Otherwise we'd loop forever
           reopening the same stale file. */
        if (st.st_size != 0) {
            debug("lock file '%s' has stale marker, retrying", lockPath.string());
            if (st.st_nlink > 0) {
                unlink(lockPath.c_str());
            }
            fd.close();
            continue;
        }

        /* Check 2: File was unlinked (catches crash-during-delete scenario) */
        if (st.st_nlink == 0) {
            debug("lock file '%s' was unlinked, retrying", lockPath.string());
            fd.close();
            continue;
        }

        /* Check 3: Verify inode matches current path (catches new-file race) */
        struct stat st_path;
        if (stat(lockPath.c_str(), &st_path) != 0) {
            debug("lock file '%s' stat failed (likely deleted), retrying", lockPath.string());
            fd.close();
            continue;
        }
        if (st.st_ino != st_path.st_ino || st.st_dev != st_path.st_dev) {
            debug(
                "lock file '%s' inode mismatch (fd: %lu, path: %lu), retrying",
                lockPath.string(),
                (unsigned long) st.st_ino,
                (unsigned long) st_path.st_ino);
            fd.close();
            continue;
        }

        break;
    }

    return fd;
}

bool PathLocks::lockPaths(const std::set<std::filesystem::path> & paths, const std::string & waitMsg, bool wait)
{
    assert(fds.empty());

    /* Note that `fds' is built incrementally so that the destructor
       will only release those locks that we have already acquired. */

    /* Acquire the lock for each path in sorted order. This ensures
       that locks are always acquired in the same order, thus
       preventing deadlocks. */
    for (auto & path : paths) {
        checkInterrupt();
        std::filesystem::path lockPath = path + ".lock";

        debug("locking path %1%", path);

        AutoCloseFD fd;

        while (1) {

            /* Open/create the lock file. */
            fd = openLockFile(lockPath, true);

            /* Acquire an exclusive lock. */
            if (!lockFile(fd.get(), ltWrite, false)) {
                if (wait) {
                    if (waitMsg != "")
                        printError(waitMsg);
                    lockFile(fd.get(), ltWrite, true);
                } else {
                    /* Failed to lock this path; release all other
                       locks. */
                    unlock();
                    return false;
                }
            }

            debug("lock acquired on %1%", lockPath);

            /* Check that the lock file hasn't become stale (i.e.,
               hasn't been unlinked).

               Note: PathLocks only checks st_size for staleness, unlike
               the fetch/substitution locks which also check st_nlink == 0
               and inode mismatch. This simpler check is sufficient here
               because PathLocks is used for store path build locking,
               where the lock files are typically long-lived and not subject
               to the same startup cleanup that fetch/substitution locks use.
               The three-way detection in fetch/substitution locks is needed
               because those cleanup stale lock files on startup, which can
               race with concurrent lock acquisition. */
            struct stat st;
            if (fstat(fd.get(), &st) == -1)
                throw SysError("statting lock file %1%", lockPath);
            if (st.st_size != 0)
                /* This lock file has been unlinked, so we're holding
                   a lock on a deleted file.  This means that other
                   processes may create and acquire a lock on
                   `lockPath', and proceed.  So we must retry. */
                debug("open lock file %1% has become stale", lockPath);
            else
                break;
        }

        /* Use borrow so that the descriptor isn't closed. */
        fds.push_back(FDPair(fd.release(), lockPath));
    }

    return true;
}

void PathLocks::unlock()
{
    for (auto & i : fds) {
        if (deletePaths)
            deleteLockFile(i.second, i.first);

        if (close(i.first) == -1)
            printError("error (ignored): cannot close lock file on %1%", i.second);

        debug("lock released on %1%", i.second);
    }

    fds.clear();
}

FdLock::FdLock(Descriptor desc, LockType lockType, bool wait, std::string_view waitMsg)
    : desc(desc)
{
    if (wait) {
        acquired = lockFile(desc, lockType, false);
        if (!acquired) {
            printInfo("%s", waitMsg);
            acquired = lockFile(desc, lockType, true);
        }
    } else
        acquired = lockFile(desc, lockType, false);
}

} // namespace nix
