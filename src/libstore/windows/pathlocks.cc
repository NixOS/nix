#include "nix/util/logging.hh"
#include "nix/store/pathlocks.hh"
#include "nix/util/signals.hh"
#include "nix/util/util.hh"

#include <chrono>
#include <thread>

#ifdef _WIN32
#  include <errhandlingapi.h>
#  include <fileapi.h>
#  include <windows.h>

using namespace std::chrono_literals;

namespace nix {

using namespace nix::windows;

void deleteLockFile(const std::filesystem::path & path, Descriptor desc)
{
    /* Delete file first, then write marker (matching Unix behavior).
       IMPORTANT: Only write the stale marker if delete succeeds.
       If delete fails but writeFull succeeds, the file would be
       permanently poisoned with the stale marker. */
    if (DeleteFileW(path.c_str()) != 0) {
        try {
            writeFull(desc, "d");
        } catch (...) {
            /* Ignore - file is deleted, waiters detect via other means */
        }
    } else {
        /* Delete failed - log but don't poison the file with stale marker */
        warn("%s: %s", path, std::to_string(GetLastError()));
    }
}

void PathLocks::unlock()
{
    for (auto & i : fds) {
        if (deletePaths)
            deleteLockFile(i.second, i.first);

        if (CloseHandle(i.first) == 0)
            printError("error (ignored): cannot close lock file on %1%", i.second);

        debug("lock released on %1%", i.second);
    }

    fds.clear();
}

AutoCloseFD openLockFile(const std::filesystem::path & path, bool create)
{
    AutoCloseFD desc = CreateFileW(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        create ? OPEN_ALWAYS : OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_POSIX_SEMANTICS,
        NULL);
    if (desc.get() == INVALID_HANDLE_VALUE)
        warn("%s: %s", path, std::to_string(GetLastError()));

    return desc;
}

bool lockFile(Descriptor desc, LockType lockType, bool wait)
{
    switch (lockType) {
    case ltNone: {
        OVERLAPPED ov = {0};
        if (!UnlockFileEx(desc, 0, 2, 0, &ov)) {
            WinError winError("Failed to unlock file desc %s", desc);
            throw winError;
        }
        return true;
    }
    case ltRead: {
        OVERLAPPED ov = {0};
        if (!LockFileEx(desc, wait ? 0 : LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov)) {
            WinError winError("Failed to lock file desc %s", desc);
            if (winError.lastError == ERROR_LOCK_VIOLATION)
                return false;
            throw winError;
        }

        ov.Offset = 1;
        if (!UnlockFileEx(desc, 0, 1, 0, &ov)) {
            WinError winError("Failed to unlock file desc %s", desc);
            if (winError.lastError != ERROR_NOT_LOCKED)
                throw winError;
        }
        return true;
    }
    case ltWrite: {
        OVERLAPPED ov = {0};
        ov.Offset = 1;
        if (!LockFileEx(desc, LOCKFILE_EXCLUSIVE_LOCK | (wait ? 0 : LOCKFILE_FAIL_IMMEDIATELY), 0, 1, 0, &ov)) {
            WinError winError("Failed to lock file desc %s", desc);
            if (winError.lastError == ERROR_LOCK_VIOLATION)
                return false;
            throw winError;
        }

        ov.Offset = 0;
        if (!UnlockFileEx(desc, 0, 1, 0, &ov)) {
            WinError winError("Failed to unlock file desc %s", desc);
            if (winError.lastError != ERROR_NOT_LOCKED)
                throw winError;
        }
        return true;
    }
    default:
        assert(false);
    }
}

bool PathLocks::lockPaths(const std::set<std::filesystem::path> & paths, const std::string & waitMsg, bool wait)
{
    assert(fds.empty());

    for (auto & path : paths) {
        checkInterrupt();
        std::filesystem::path lockPath = path;
        lockPath += L".lock";
        debug("locking path %1%", path);

        AutoCloseFD fd;

        while (1) {
            fd = openLockFile(lockPath, true);
            if (!lockFile(fd.get(), ltWrite, false)) {
                if (wait) {
                    if (waitMsg != "")
                        printError(waitMsg);
                    lockFile(fd.get(), ltWrite, true);
                } else {
                    unlock();
                    return false;
                }
            }

            debug("lock acquired on %1%", lockPath);

            struct _stat st;
            if (_fstat(fromDescriptorReadOnly(fd.get()), &st) == -1)
                throw SysError("statting lock file %1%", lockPath);
            if (st.st_size != 0)
                debug("open lock file %1% has become stale", lockPath);
            else
                break;
        }

        fds.push_back(FDPair(fd.release(), lockPath));
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
     * Windows doesn't have a native flock() with timeout. We use a polling
     * approach with exponential backoff, similar to Unix implementation.
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
           On Windows, we use _fstat to check file properties.

           Note: Windows doesn't have st_nlink in the same way as Unix.
           We rely on st_size check and the fact that with FILE_SHARE_DELETE,
           DeleteFileW marks the file for deletion but it remains accessible
           until all handles are closed. The file size check is sufficient
           because deleteLockFile only writes the marker after successful delete. */
        struct _stat64 st;
        if (_fstat64(fromDescriptorReadOnly(fd.get()), &st) == -1)
            throw SysError("statting lock file '%s'", lockPath.string());

        /* Check for stale marker written by previous holder */
        if (st.st_size != 0) {
            debug("lock file '%s' has stale marker, retrying", lockPath.string());
            /* Try to delete the stale file so next iteration gets a fresh one */
            DeleteFileW(lockPath.c_str());
            fd.close();
            continue;
        }

        /* On Windows, also verify the file still exists at the expected path.
           This catches the case where the file was deleted and recreated. */
        if (!std::filesystem::exists(lockPath)) {
            debug("lock file '%s' no longer exists, retrying", lockPath.string());
            fd.close();
            continue;
        }

        break;
    }

    return fd;
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
#endif
