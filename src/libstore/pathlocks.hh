#pragma once

#include "util.hh"

namespace nix {

/* Open (possibly create) a lock file and return the file descriptor.
   -1 is returned if create is false and the lock could not be opened
   because it doesn't exist.  Any other error throws an exception. */
#ifndef _WIN32
AutoCloseFD openLockFile(const Path & path, bool create);
#else
AutoCloseWindowsHandle openLockFile(const Path & path, bool create);
#endif

/* Delete an open lock file. */
#ifndef _WIN32
void deleteLockFile(const Path & path, int fd);
#else
void deleteLockFile(const Path & path);
#endif

enum LockType { ltRead, ltWrite, ltNone };

#ifndef _WIN32
bool lockFile(int fd, LockType lockType, bool wait);
#else
bool lockFile(HANDLE handle, LockType lockType, bool wait);
#endif

class PathLocks
{
private:
#ifndef _WIN32
    typedef std::pair<int, Path> FDPair;
#else
    typedef std::pair<HANDLE, Path> FDPair;
#endif
    list<FDPair> fds;
    bool deletePaths;

public:
    PathLocks();
    PathLocks(const PathSet & paths,
        const string & waitMsg = "");
    bool lockPaths(const PathSet & _paths,
        const string & waitMsg = "",
        bool wait = true);
    ~PathLocks();
    void unlock();
    void setDeletion(bool deletePaths);
};

}
