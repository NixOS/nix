#ifndef __PATHLOCKS_H
#define __PATHLOCKS_H

#include "types.hh"


namespace nix {


/* Open (possibly create) a lock file and return the file descriptor.
   -1 is returned if create is false and the lock could not be opened
   because it doesn't exist.  Any other error throws an exception. */
int openLockFile(const Path & path, bool create);

/* Delete an open lock file.  Both must be called to be fully portable
   between Unix and Windows. */
void deleteLockFilePreClose(const Path & path, int fd);
void deleteLockFilePostClose(const Path & path);

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
        const string & waitMsg = "");
    void lockPaths(const PathSet & _paths,
        const string & waitMsg = "");
    ~PathLocks();
    void unlock();
    void setDeletion(bool deletePaths);
};


bool pathIsLockedByMe(const Path & path);


}


#endif /* !__PATHLOCKS_H */
