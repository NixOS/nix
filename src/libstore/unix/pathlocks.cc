#include "nix/store/pathlocks.hh"
#include "nix/util/file-system-at.hh"
#include "nix/util/signals.hh"
#include "nix/util/sync.hh"

#include <cerrno>
#include <cstdlib>
#include <set>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>

namespace nix {

/* Tracks paths currently locked by some `PathLocks` instance in this
   process. Used by `addToStore` to skip a second `flock(LOCK_EX)` on a
   path the caller already holds, which would otherwise self-deadlock
   because Linux flock is per-open-file-description. */
static Sync<std::set<std::filesystem::path>> & heldRealPaths()
{
    static Sync<std::set<std::filesystem::path>> s;
    return s;
}

bool PathLocks::isHeldByThisProcess(const std::filesystem::path & realPath)
{
    auto held(heldRealPaths().lock());
    return held->count(realPath) > 0;
}

AutoCloseFD openLockFile(const std::filesystem::path & path, bool create)
{
    AutoCloseFD fd;

    fd = open(path.c_str(), O_CLOEXEC | O_RDWR | (create ? O_CREAT : 0), 0600);
    if (!fd && (create || errno != ENOENT))
        throw SysError("opening lock file %1%", PathFmt(path));

    return fd;
}

void deleteLockFile(const std::filesystem::path & path, Descriptor desc)
{
    /* Get rid of the lock file.  Have to be careful not to introduce
       races.  Write a (meaningless) token to the file to indicate to
       other processes waiting on this lock that the lock is stale
       (deleted). */
    tryUnlink(path);
    writeFull(desc, "d");
    /* We just try to unlink don't care if it fails; removing the lock
       file is an optimisation, not a necessity. */
}

bool lockFile(Descriptor desc, LockType lockType, bool wait)
{
    int type;
    if (lockType == ltRead)
        type = LOCK_SH;
    else if (lockType == ltWrite)
        type = LOCK_EX;
    else if (lockType == ltNone)
        type = LOCK_UN;
    else
        unreachable();

    if (wait) {
        while (flock(desc, type) != 0) {
            checkInterrupt();
            if (errno != EINTR)
                throw SysError("acquiring/releasing lock");
            else
                return false;
        }
    } else {
        while (flock(desc, type | LOCK_NB) != 0) {
            checkInterrupt();
            if (errno == EWOULDBLOCK)
                return false;
            if (errno != EINTR)
                throw SysError("acquiring/releasing lock");
        }
    }

    return true;
}

bool PathLocks::lockPaths(const std::set<std::filesystem::path> & paths, const std::string & waitMsg, bool wait)
{
    assert(fds.empty());

    /* Note that `fds' is built incrementally so that the destructor
       will only release those locks that we have already acquired. */

    /* Acquire the lock for each path in sorted order. This ensures
       that locks are always acquired in the same order, thus
       preventing deadlocks. */
    for (auto & path : paths) {
        checkInterrupt();
        auto lockPath = path;
        lockPath += ".lock";

        debug("locking path %1%", PathFmt(path));

        AutoCloseFD fd;

        while (1) {

            /* Open/create the lock file. */
            fd = openLockFile(lockPath, true);

            /* Acquire an exclusive lock. */
            if (!lockFile(fd.get(), ltWrite, false)) {
                if (wait) {
                    if (waitMsg != "")
                        printError(waitMsg);
                    lockFile(fd.get(), ltWrite, true);
                } else {
                    /* Failed to lock this path; release all other
                       locks. */
                    unlock();
                    return false;
                }
            }

            debug("lock acquired on %1%", PathFmt(lockPath));

            /* Check that the lock file hasn't become stale (i.e.,
               hasn't been unlinked). */
            auto st = nix::fstat(fd.get());
            if (st.st_size != 0)
                /* This lock file has been unlinked, so we're holding
                   a lock on a deleted file.  This means that other
                   processes may create and acquire a lock on
                   `lockPath', and proceed.  So we must retry. */
                debug("open lock file %1% has become stale", PathFmt(lockPath));
            else
                break;
        }

        /* Register the (real, non-`.lock`) path as held by this process
           so `addToStore` can detect re-entry and skip a self-deadlocking
           second `flock`. Done before `fds.push_back` so that if the
           push throws (e.g. `bad_alloc`), `isHeldByThisProcess` still
           reports the kernel-held flock and re-entrant callers won't
           attempt a second, deadlocking acquisition. */
        {
            auto held(heldRealPaths().lock());
            held->insert(path);
        }

        /* Use borrow so that the descriptor isn't closed. */
        fds.push_back(FDPair(fd.release(), lockPath));
    }

    return true;
}

void PathLocks::unlock()
{
    for (auto & i : fds) {
        if (deletePaths)
            deleteLockFile(i.second, i.first);

        if (close(i.first) == -1)
            printError("error (ignored): cannot close lock file on %1%", PathFmt(i.second));

        debug("lock released on %1%", PathFmt(i.second));

        /* Mirror the insert in `lockPaths()`: derive the real path from
           `lockPath` by stripping the trailing `.lock`. */
        auto realPath = i.second;
        realPath.replace_extension();
        {
            auto held(heldRealPaths().lock());
            held->erase(realPath);
        }
    }

    fds.clear();
}

FdLock::FdLock(Descriptor desc, LockType lockType, bool wait, std::string_view waitMsg)
    : desc(desc)
{
    if (wait) {
        if (!lockFile(desc, lockType, false)) {
            printInfo("%s", waitMsg);
            acquired = lockFile(desc, lockType, true);
        }
    } else
        acquired = lockFile(desc, lockType, false);
}

} // namespace nix
