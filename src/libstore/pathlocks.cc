#include <cerrno>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "pathlocks.hh"


bool lockFile(int fd, LockType lockType, bool wait)
{
    struct flock lock;
    if (lockType == ltRead) lock.l_type = F_RDLCK;
    else if (lockType == ltWrite) lock.l_type = F_WRLCK;
    else if (lockType == ltNone) lock.l_type = F_UNLCK;
    else abort();
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0; /* entire file */

    if (wait) {
        while (fcntl(fd, F_SETLKW, &lock) != 0) {
            checkInterrupt();
            if (errno != EINTR)
                throw SysError(format("acquiring/releasing lock"));
        }
    } else {
        while (fcntl(fd, F_SETLK, &lock) != 0) {
            checkInterrupt();
            if (errno == EACCES || errno == EAGAIN) return false;
            if (errno != EINTR) 
                throw SysError(format("acquiring/releasing lock"));
        }
    }

    return true;
}


/* This enables us to check whether are not already holding a lock on
   a file ourselves.  POSIX locks (fcntl) suck in this respect: if we
   close a descriptor, the previous lock will be closed as well.  And
   there is no way to query whether we already have a lock (F_GETLK
   only works on locks held by other processes). */
static StringSet lockedPaths; /* !!! not thread-safe */


PathLocks::PathLocks()
    : deletePaths(false)
{
}


PathLocks::PathLocks(const PathSet & paths)
    : deletePaths(false)
{
    lockPaths(paths);
}


void PathLocks::lockPaths(const PathSet & _paths)
{
    /* May be called only once! */
    assert(this->paths.empty());
    
    /* Note that `fds' is built incrementally so that the destructor
       will only release those locks that we have already acquired. */

    /* Sort the paths.  This assures that locks are always acquired in
       the same order, thus preventing deadlocks. */
    Paths paths(_paths.begin(), _paths.end());
    paths.sort();
    
    /* Acquire the lock for each path. */
    for (Paths::iterator i = paths.begin(); i != paths.end(); i++) {
        checkInterrupt();
        Path path = *i;
        Path lockPath = path + ".lock";

        debug(format("locking path `%1%'") % path);

        if (lockedPaths.find(lockPath) != lockedPaths.end()) {
            debug(format("already holding lock on `%1%'") % lockPath);
            continue;
        }
        
        /* Open/create the lock file. */
        int fd = open(lockPath.c_str(), O_WRONLY | O_CREAT, 0666);
        if (fd == -1)
            throw SysError(format("opening lock file `%1%'") % lockPath);

        fds.push_back(fd);
        this->paths.push_back(lockPath);

        /* Acquire an exclusive lock. */
        lockFile(fd, ltWrite, true);

        debug(format("lock acquired on `%1%'") % lockPath);

        lockedPaths.insert(lockPath);
    }
}


PathLocks::~PathLocks()
{
    for (list<int>::iterator i = fds.begin(); i != fds.end(); i++)
        if (close(*i) != 0) throw SysError("closing fd");

    for (Paths::iterator i = paths.begin(); i != paths.end(); i++) {
        checkInterrupt();
        if (deletePaths) {
            /* This is not safe in general! */
            unlink(i->c_str());
            /* Note that the result of unlink() is ignored; removing
               the lock file is an optimisation, not a necessity. */
        }
        lockedPaths.erase(*i);
        debug(format("lock released on `%1%'") % *i);
    }
}


void PathLocks::setDeletion(bool deletePaths)
{
    this->deletePaths = deletePaths;
}
