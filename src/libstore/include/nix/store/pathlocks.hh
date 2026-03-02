#pragma once
///@file

#include <filesystem>

#include "nix/util/file-descriptor.hh"

namespace nix {

/**
 * Open (possibly create) a lock file and return the file descriptor.
 * -1 is returned if create is false and the lock could not be opened
 * because it doesn't exist.  Any other error throws an exception.
 */
AutoCloseFD openLockFile(const std::filesystem::path & path, bool create);

/**
 * Delete an open lock file.
 */
void deleteLockFile(const std::filesystem::path & path, Descriptor desc);

enum LockType { ltRead, ltWrite, ltNone };

/**
 * Acquire or release a lock on a file descriptor using flock().
 *
 * @param desc File descriptor to lock
 * @param lockType Type of lock: ltRead (shared), ltWrite (exclusive), or ltNone (unlock)
 * @param wait If true, block until lock is acquired; if false, return immediately
 * @return true if lock was acquired/released, false if would block (when wait=false)
 * @throws SysError on system errors
 */
bool lockFile(Descriptor desc, LockType lockType, bool wait);

/**
 * Try to acquire a lock with a timeout.
 *
 * @param desc File descriptor to lock
 * @param lockType Type of lock (read/write/none)
 * @param timeout Timeout in seconds (0 = no timeout, wait indefinitely)
 * @return true if lock was acquired, false if timed out
 * @throws SysError on system errors
 */
bool lockFileWithTimeout(Descriptor desc, LockType lockType, unsigned int timeout);

/**
 * Acquire an exclusive file lock with stale detection.
 *
 * This function handles the complete lock acquisition process:
 * 1. Opens/creates the lock file
 * 2. Acquires an exclusive (write) lock with timeout
 * 3. Detects and handles stale lock files (via st_size, st_nlink, inode checks)
 * 4. Retries automatically if the lock file was stale
 *
 * A lock file is considered stale if:
 * - st_size != 0: previous holder wrote a stale marker via deleteLockFile()
 * - st_nlink == 0: file was unlinked while we were waiting
 * - inode mismatch: a new file was created at the same path
 *
 * @param lockPath Path to the lock file
 * @param timeout Lock timeout in seconds (0 = wait indefinitely)
 * @param identity Human-readable identity for log messages (e.g., URL or hash)
 * @return The acquired file descriptor (caller must call deleteLockFile on cleanup)
 * @throws Error if lock file cannot be opened or timeout is exceeded
 */
AutoCloseFD
acquireExclusiveFileLock(const std::filesystem::path & lockPath, unsigned int timeout, std::string_view identity);

class PathLocks
{
private:
    typedef std::pair<Descriptor, std::filesystem::path> FDPair;
    std::list<FDPair> fds;
    bool deletePaths;

public:
    PathLocks();
    PathLocks(const std::set<std::filesystem::path> & paths, const std::string & waitMsg = "");

    PathLocks(PathLocks && other) noexcept
        : fds(std::exchange(other.fds, {}))
        , deletePaths(other.deletePaths)
    {
    }

    PathLocks & operator=(PathLocks && other) noexcept
    {
        fds = std::exchange(other.fds, {});
        deletePaths = other.deletePaths;
        return *this;
    }

    PathLocks(const PathLocks &) = delete;
    PathLocks & operator=(const PathLocks &) = delete;
    bool lockPaths(const std::set<std::filesystem::path> & _paths, const std::string & waitMsg = "", bool wait = true);
    ~PathLocks();
    void unlock();
    void setDeletion(bool deletePaths);
};

struct FdLock
{
    Descriptor desc;
    bool acquired = false;

    FdLock(Descriptor desc, LockType lockType, bool wait, std::string_view waitMsg);
    FdLock(const FdLock &) = delete;
    FdLock & operator=(const FdLock &) = delete;
    FdLock(FdLock &&) = delete;
    FdLock & operator=(FdLock &&) = delete;

    ~FdLock()
    {
        if (acquired)
            lockFile(desc, ltNone, false);
    }
};

} // namespace nix
