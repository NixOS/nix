#pragma once
///@file

#include "file-descriptor.hh"

namespace nix {

/**
 * Open (possibly create) a lock file and return the file descriptor.
 * -1 is returned if create is false and the lock could not be opened
 * because it doesn't exist.  Any other error throws an exception.
 */
AutoCloseFD openLockFile(const Path & path, bool create);

/**
 * Delete an open lock file.
 */
void deleteLockFile(const Path & path, Descriptor desc);

enum LockType { ltRead, ltWrite, ltNone };

bool lockFile(Descriptor desc, LockType lockType, bool wait);

class PathLocks
{
private:
    typedef std::pair<Descriptor, Path> FDPair;
    std::list<FDPair> fds;
    bool deletePaths;

public:
    PathLocks();
    PathLocks(const PathSet & paths,
        const std::string & waitMsg = "");
    bool lockPaths(const PathSet & _paths,
        const std::string & waitMsg = "",
        bool wait = true);
    ~PathLocks();
    void unlock();
    void setDeletion(bool deletePaths);
};

struct FdLock
{
    Descriptor desc;
    bool acquired = false;

    FdLock(Descriptor desc, LockType lockType, bool wait, std::string_view waitMsg);

    ~FdLock()
    {
        if (acquired)
            lockFile(desc, ltNone, false);
    }
};

}
