#include "pathlocks.hh"
#include "util.hh"
#include "sync.hh"

#include <cerrno>
#include <cstdlib>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


namespace nix {


int openLockFile(const Path & path, bool create)
{
    AutoCloseFD fd;

    fd = open(path.c_str(), O_CLOEXEC | O_RDWR | (create ? O_CREAT : 0), 0600);
    if (!fd && (create || errno != ENOENT))
        throw SysError(format("opening lock file ‘%1%’") % path);

    return fd.release();
}


void deleteLockFile(const Path & path, int fd)
{
    /* Get rid of the lock file.  Have to be careful not to introduce
       races.  Write a (meaningless) token to the file to indicate to
       other processes waiting on this lock that the lock is stale
       (deleted). */
    unlink(path.c_str());
    writeFull(fd, "d");
    /* Note that the result of unlink() is ignored; removing the lock
       file is an optimisation, not a necessity. */
}


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
static Sync<StringSet> lockedPaths_;


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
    for (auto & path : paths) {
        checkInterrupt();
        Path lockPath = path + ".lock";

        debug(format("locking path ‘%1%’") % path);

        {
            auto lockedPaths(lockedPaths_.lock());
            if (lockedPaths->count(lockPath))
                throw Error("deadlock: trying to re-acquire self-held lock ‘%s’", lockPath);
            lockedPaths->insert(lockPath);
        }

        try {

            AutoCloseFD fd;

            while (1) {

                /* Open/create the lock file. */
                fd = openLockFile(lockPath, true);

                /* Acquire an exclusive lock. */
                if (!lockFile(fd.get(), ltWrite, false)) {
                    if (wait) {
                        if (waitMsg != "") printError(waitMsg);
                        lockFile(fd.get(), ltWrite, true);
                    } else {
                        /* Failed to lock this path; release all other
                           locks. */
                        unlock();
                        return false;
                    }
                }

                debug(format("lock acquired on ‘%1%’") % lockPath);

                /* Check that the lock file hasn't become stale (i.e.,
                   hasn't been unlinked). */
                struct stat st;
                if (fstat(fd.get(), &st) == -1)
                    throw SysError(format("statting lock file ‘%1%’") % lockPath);
                if (st.st_size != 0)
                    /* This lock file has been unlinked, so we're holding
                       a lock on a deleted file.  This means that other
                       processes may create and acquire a lock on
                       `lockPath', and proceed.  So we must retry. */
                    debug(format("open lock file ‘%1%’ has become stale") % lockPath);
                else
                    break;
            }

            /* Use borrow so that the descriptor isn't closed. */
            fds.push_back(FDPair(fd.release(), lockPath));

        } catch (...) {
            lockedPaths_.lock()->erase(lockPath);
            throw;
        }

    }

    return true;
}


PathLocks::~PathLocks()
{
    try {
        unlock();
    } catch (...) {
        ignoreException();
    }
}


void PathLocks::unlock()
{
    for (auto & i : fds) {
        if (deletePaths) deleteLockFile(i.second, i.first);

        lockedPaths_.lock()->erase(i.second);

        if (close(i.first) == -1)
            printError(
                format("error (ignored): cannot close lock file on ‘%1%’") % i.second);

        debug(format("lock released on ‘%1%’") % i.second);
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
    return lockedPaths_.lock()->count(lockPath);
}


}
