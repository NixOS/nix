#include "pathlocks.hh"
#include "util.hh"

#include <cerrno>
#include <cstdlib>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


namespace nix {


int openLockFile(const Path & path, bool create)
{
    AutoCloseFD fd;

    fd = open(path.c_str(), O_RDWR | (create ? O_CREAT : 0), 0666);
    if (fd == -1 && (create || errno != ENOENT))
        throw SysError(format("opening lock file `%1%'") % path);

    return fd.borrow();
}


void deleteLockFile(const Path & path, int fd)
{
    /* Get rid of the lock file.  Have to be careful not to introduce
       races.  Write a (meaningless) token to the file to indicate to
       other processes waiting on this lock that the lock is stale
       (deleted). */
    unlink(path.c_str());
    writeFull(fd, (const unsigned char *) "d", 1);
    /* Note that the result of unlink() is ignored; removing the lock
       file is an optimisation, not a necessity. */
}


bool lockFile(int fd, LockType lockType, bool wait,
    unsigned int progressInterval)
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
        /* Wait until we acquire the lock.  If `progressInterval' is
           non-zero, when print a message every `progressInterval'
           seconds.  This is mostly to make sure that remote builders
           aren't killed due to the `max-silent-time' inactivity
           monitor while waiting for the garbage collector lock. */
        while (1) {
            if (progressInterval) alarm(progressInterval);
            if (fcntl(fd, F_SETLKW, &lock) == 0) break;
            checkInterrupt();
            if (errno != EINTR)
                throw SysError(format("acquiring/releasing lock"));
            if (progressInterval) printMsg(lvlError, "still waiting for lock...");
        }
        alarm(0);
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


PathLocks::PathLocks(const PathSet & paths, const string & waitMsg)
    : deletePaths(false)
{
    lockPaths(paths, waitMsg);
}


bool PathLocks::lockPaths(const PathSet & _paths,
    const string & waitMsg, bool wait)
{
    assert(fds.empty());
    
    /* Note that `fds' is built incrementally so that the destructor
       will only release those locks that we have already acquired. */

    /* Sort the paths.  This assures that locks are always acquired in
       the same order, thus preventing deadlocks. */
    Paths paths(_paths.begin(), _paths.end());
    paths.sort();
    
    /* Acquire the lock for each path. */
    foreach (Paths::iterator, i, paths) {
        checkInterrupt();
        Path path = *i;
        Path lockPath = path + ".lock";

        debug(format("locking path `%1%'") % path);

        if (lockedPaths.find(lockPath) != lockedPaths.end())
            throw Error("deadlock: trying to re-acquire self-held lock");

        AutoCloseFD fd;
        
        while (1) {

            /* Open/create the lock file. */
	    fd = openLockFile(lockPath, true);

            /* Acquire an exclusive lock. */
            if (!lockFile(fd, ltWrite, false)) {
                if (wait) {
                    if (waitMsg != "") printMsg(lvlError, waitMsg);
                    lockFile(fd, ltWrite, true);
                } else {
                    /* Failed to lock this path; release all other
                       locks. */
                    unlock();
                    return false;
                }
            }

            debug(format("lock acquired on `%1%'") % lockPath);

            /* Check that the lock file hasn't become stale (i.e.,
               hasn't been unlinked). */
            struct stat st;
            if (fstat(fd, &st) == -1)
                throw SysError(format("statting lock file `%1%'") % lockPath);
            if (st.st_size != 0)
                /* This lock file has been unlinked, so we're holding
                   a lock on a deleted file.  This means that other
                   processes may create and acquire a lock on
                   `lockPath', and proceed.  So we must retry. */
                debug(format("open lock file `%1%' has become stale") % lockPath);
            else
                break;
        }

        /* Use borrow so that the descriptor isn't closed. */
        fds.push_back(FDPair(fd.borrow(), lockPath));
        lockedPaths.insert(lockPath);
    }

    return true;
}


PathLocks::~PathLocks()
{
    unlock();
}


void PathLocks::unlock()
{
    foreach (list<FDPair>::iterator, i, fds) {
        if (deletePaths) deleteLockFile(i->second, i->first);

        lockedPaths.erase(i->second);
        if (close(i->first) == -1)
            printMsg(lvlError,
                format("error (ignored): cannot close lock file on `%1%'") % i->second);

        debug(format("lock released on `%1%'") % i->second);
    }

    fds.clear();
}


void PathLocks::setDeletion(bool deletePaths)
{
    this->deletePaths = deletePaths;
}


bool pathIsLockedByMe(const Path & path)
{
    Path lockPath = path + ".lock";
    return lockedPaths.find(lockPath) != lockedPaths.end();
}

 
}
