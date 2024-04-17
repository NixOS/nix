#include "pathlocks.hh"
#include "util.hh"
#include "sync.hh"
#include "signals.hh"

#include <cerrno>
#include <cstdlib>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>


namespace nix {

using namespace nix::unix;

AutoCloseFD unix::openLockFile(const Path & path, bool create)
{
    AutoCloseFD fd;

    fd = open(path.c_str(), O_CLOEXEC | O_RDWR | (create ? O_CREAT : 0), 0600);
    if (!fd && (create || errno != ENOENT))
        throw SysError("opening lock file '%1%'", path);

    return fd;
}


void unix::deleteLockFile(const Path & path, int fd)
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


bool unix::lockFile(int fd, LockType lockType, bool wait)
{
    int type;
    if (lockType == ltRead) type = LOCK_SH;
    else if (lockType == ltWrite) type = LOCK_EX;
    else if (lockType == ltNone) type = LOCK_UN;
    else abort();

    if (wait) {
        while (flock(fd, type) != 0) {
            checkInterrupt();
            if (errno != EINTR)
                throw SysError("acquiring/releasing lock");
            else
                return false;
        }
    } else {
        while (flock(fd, type | LOCK_NB) != 0) {
            checkInterrupt();
            if (errno == EWOULDBLOCK) return false;
            if (errno != EINTR)
                throw SysError("acquiring/releasing lock");
        }
    }

    return true;
}


bool PathLocks::lockPaths(const PathSet & paths,
    const std::string & waitMsg, bool wait)
{
    assert(fds.empty());

    /* Note that `fds' is built incrementally so that the destructor
       will only release those locks that we have already acquired. */

    /* Acquire the lock for each path in sorted order. This ensures
       that locks are always acquired in the same order, thus
       preventing deadlocks. */
    for (auto & path : paths) {
        checkInterrupt();
        Path lockPath = path + ".lock";

        debug("locking path '%1%'", path);

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

            debug("lock acquired on '%1%'", lockPath);

            /* Check that the lock file hasn't become stale (i.e.,
               hasn't been unlinked). */
            struct stat st;
            if (fstat(fd.get(), &st) == -1)
                throw SysError("statting lock file '%1%'", lockPath);
            if (st.st_size != 0)
                /* This lock file has been unlinked, so we're holding
                   a lock on a deleted file.  This means that other
                   processes may create and acquire a lock on
                   `lockPath', and proceed.  So we must retry. */
                debug("open lock file '%1%' has become stale", lockPath);
            else
                break;
        }

        /* Use borrow so that the descriptor isn't closed. */
        fds.push_back(FDPair(fd.release(), lockPath));
    }

    return true;
}


void PathLocks::unlock()
{
    for (auto & i : fds) {
        if (deletePaths) deleteLockFile(i.second, i.first);

        if (close(i.first) == -1)
            printError(
                "error (ignored): cannot close lock file on '%1%'",
                i.second);

        debug("lock released on '%1%'", i.second);
    }

    fds.clear();
}


FdLock::FdLock(int fd, LockType lockType, bool wait, std::string_view waitMsg)
    : fd(fd)
{
    if (wait) {
        if (!lockFile(fd, lockType, false)) {
            printInfo("%s", waitMsg);
            acquired = lockFile(fd, lockType, true);
        }
    } else
        acquired = lockFile(fd, lockType, false);
}


}
