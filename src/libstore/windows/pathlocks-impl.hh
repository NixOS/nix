#pragma once
///@file Needed because Unix-specific counterpart
///
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
void deleteLockFile(const Path & path, HANDLE handle);

enum LockType { ltRead, ltWrite, ltNone };

bool lockFile(HANDLE handle, LockType lockType, bool wait);

struct FdLock
{
    HANDLE handle;
    bool acquired = false;

    FdLock(HANDLE handle, LockType lockType, bool wait, std::string_view waitMsg);

    ~FdLock()
    {
        if (acquired)
            lockFile(handle, ltNone, false);
    }
};

}
