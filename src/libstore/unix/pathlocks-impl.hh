#pragma once
///@file

#include "file-descriptor.hh"

namespace nix::unix {

/**
 * Open (possibly create) a lock file and return the file descriptor.
 * -1 is returned if create is false and the lock could not be opened
 * because it doesn't exist.  Any other error throws an exception.
 */
AutoCloseFD openLockFile(const Path & path, bool create);

/**
 * Delete an open lock file.
 */
void deleteLockFile(const Path & path, int fd);

enum LockType { ltRead, ltWrite, ltNone };

bool lockFile(int fd, LockType lockType, bool wait);

struct FdLock
{
    int fd;
    bool acquired = false;

    FdLock(int fd, LockType lockType, bool wait, std::string_view waitMsg);

    ~FdLock()
    {
        if (acquired)
            lockFile(fd, ltNone, false);
    }
};

}
