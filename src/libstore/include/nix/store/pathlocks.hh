#pragma once
///@file

#include <filesystem>

#include "nix/util/file-descriptor.hh"
#include "nix/util/os-filename.hh"

namespace nix {

/**
 * Open (possibly create) a lock file and return the file descriptor.
 * -1 is returned if create is false and the lock could not be opened
 * because it doesn't exist.  Any other error throws an exception.
 */
AutoCloseFD openLockFile(const std::filesystem::path & path, bool create);

/**
 * Like `openLockFile`, but for @p name in the directory @p dirFd.
 *
 * A symlink there is not followed.
 *
 * @throws SymlinkNotAllowed if @p name is a symlink.
 */
AutoCloseFD openLockFileAt(Descriptor dirFd, const OsFilename & name, bool create);

/**
 * Delete an open lock file.
 */
void deleteLockFile(const std::filesystem::path & path, Descriptor desc);

enum LockType { ltRead, ltWrite, ltNone };

bool lockFile(Descriptor desc, LockType lockType, bool wait);

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
