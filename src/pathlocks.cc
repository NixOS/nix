#include <fcntl.h>

#include "pathlocks.hh"


/* This enables us to check whether are not already holding a lock on
   a file ourselves.  POSIX locks (fcntl) suck in this respect: if we
   close a descriptor, the previous lock will be closed as well.  And
   there is no way to query whether we already have a lock (F_GETLK
   only works on locks held by other processes). */
static StringSet lockedPaths; /* !!! not thread-safe */


PathLocks::PathLocks(const Strings & _paths)
{
    /* Note that `fds' is built incrementally so that the destructor
       will only release those locks that we have already acquired. */

    /* Sort the paths.  This assures that locks are always acquired in
       the same order, thus preventing deadlocks. */
    Strings paths(_paths);
    paths.sort();
    
    /* Acquire the lock for each path. */
    for (Strings::iterator i = paths.begin(); i != paths.end(); i++) {
        string path = *i;
        string lockPath = path + ".lock";

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

        /* Lock it. */
        struct flock lock;
        lock.l_type = F_WRLCK; /* exclusive lock */
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0; /* entire file */

        while (fcntl(fd, F_SETLKW, &lock) == -1)
            if (errno != EINTR)
                throw SysError(format("acquiring lock on `%1%'") % lockPath);

        lockedPaths.insert(lockPath);
    }
}


PathLocks::~PathLocks()
{
    for (list<int>::iterator i = fds.begin(); i != fds.end(); i++)
        close(*i);

    for (Strings::iterator i = paths.begin(); i != paths.end(); i++)
        lockedPaths.erase(*i);
}
