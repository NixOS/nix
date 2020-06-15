#pragma once

#include "util.hh"

namespace nix {

/* Open (possibly create) a lock file and return the file descriptor.
   -1 is returned if create is false and the lock could not be opened
   because it doesn't exist.  Any other error throws an exception. */
AutoCloseFD openLockFile(PathView path, bool create);

/* Delete an open lock file. */
void deleteLockFile(PathView path, int fd);

enum LockType { ltRead, ltWrite, ltNone };

bool lockFile(int fd, LockType lockType, bool wait);

class PathLocks
{
private:
    typedef std::pair<int, Path> FDPair;
    list<FDPair> fds;
    bool deletePaths;

public:
    PathLocks();
    PathLocks(const PathSet & paths,
        std::string_view waitMsg = "");
    bool lockPaths(const PathSet & _paths,
        std::string_view waitMsg = "",
        bool wait = true);
    ~PathLocks();
    void unlock();
    void setDeletion(bool deletePaths);
};

}
