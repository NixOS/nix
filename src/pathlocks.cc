#include <fcntl.h>

#include "pathlocks.hh"


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
        
        /* Open/create the lock file. */
        int fd = open(lockPath.c_str(), O_WRONLY | O_CREAT, 0666);
        if (fd == -1)
            throw SysError(format("opening lock file `%1%'") % lockPath);

        fds.push_back(fd);

        /* Lock it. */
        struct flock lock;
        lock.l_type = F_WRLCK; /* exclusive lock */
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0; /* entire file */

        while (fcntl(fd, F_SETLKW, &lock) == -1)
            if (errno != EINTR)
                throw SysError(format("acquiring lock on `%1%'") % lockPath);
    }
}


PathLocks::~PathLocks()
{
    for (list<int>::iterator i = fds.begin(); i != fds.end(); i++)
        close(*i);
}
