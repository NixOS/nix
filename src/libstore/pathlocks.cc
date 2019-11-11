#include "pathlocks.hh"
#include "util.hh"
#include "sync.hh"

#include <cerrno>
#include <cstdlib>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <iostream>
#endif

namespace nix {


#ifndef _WIN32
AutoCloseFD openLockFile(const Path & path, bool create)
{
    AutoCloseFD fd;

    fd = open(path.c_str(), O_CLOEXEC | O_RDWR | (create ? O_CREAT : 0), 0600);
    if (!fd && (create || errno != ENOENT))
        throw PosixError(format("opening lock file '%1%'") % path);

    return fd;
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
#else
AutoCloseWindowsHandle openLockFile(const Path & path, bool create)
{
    std::cerr << "openLockFile(" << path << "," << create << ") GetFileAttributesW=" << GetFileAttributesW(pathW(path).c_str()) << std::endl;

    AutoCloseWindowsHandle fd = CreateFileW(pathW(path).c_str(),
                                            GENERIC_READ | GENERIC_WRITE,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                                            NULL,
                                            create ? /*CREATE_NEW*/ OPEN_ALWAYS : OPEN_EXISTING,
                                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_POSIX_SEMANTICS,
                                            NULL);
    std::cerr << "openLockFile fd=" << fd.get() << std::endl;
    if (fd.get() == INVALID_HANDLE_VALUE)
        throw WinError("%4%:%5% openLockFile(%1%, %2%) CreateFileW '%3%'", path, create, path, __FILE__, __LINE__);

    return fd;
}

void deleteLockFile(const Path & path)
{
    if (!DeleteFileW(pathW(path).c_str())) {
        printError(WinError("DeleteFileW(%1%) in deleteLockFile", path).msg());
    }
    /* Note that the result of unlink() is ignored; removing the lock
       file is an optimisation, not a necessity.
       But leaving lock files would result in failing some tests in nix testsuite,
       those who rely on ".. | grep $outPath" will match "$outPath.lock" too
    */
}
#endif



#ifndef _WIN32
bool lockFile(int fd, LockType lockType, bool wait)
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
                throw PosixError(format("acquiring/releasing lock"));
            else
                return false;
        }
    } else {
        while (flock(fd, type | LOCK_NB) != 0) {
            checkInterrupt();
            if (errno == EWOULDBLOCK) return false;
            if (errno != EINTR)
                throw PosixError(format("acquiring/releasing lock"));
        }
    }

    return true;
}
#else
bool lockFile(HANDLE handle, LockType lockType, bool wait)
{
    /* simulating Linux: read lock region 0:1, write locks 1:2, unlock 0:2 both at once */

//  std::cerr << "lockFile handle="<<handle<<"("
//            << handleToPath(handle)<<") lockType="<<lockType<<" wait="<<wait
//            << std::endl;
    switch(lockType) {
        case ltNone: {
            OVERLAPPED ov = { /*.Offset =*/ 0 };
            if (!UnlockFileEx(handle, 0, 2, 0, &ov)) {
                WinError winError("UnlockFileEx(handle=%1%)", handle/*, handleToPath(handle)*/);
//              std::cerr << "TODO: UnlockFileEx gle="<<winError.lastError << std::endl;
                throw winError;
            }
//          std::cerr << "/lockFile handle="<<handle << std::endl;
            return true;
        }
        case ltRead: {
            OVERLAPPED ov = { /*.Offset =*/ 0 };
            if (!LockFileEx(handle, wait ? 0 : LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov)) {
                WinError winError("LockFileEx(handle=%1%)", handle/*, handleToPath(handle)*/);
//              std::cerr << "TODO: LockFileEx gle="<<winError.lastError << std::endl;
                if (winError.lastError == ERROR_LOCK_VIOLATION)
                    return false;
                throw winError;
            }
//          std::cerr << "LockFileEx ok? gle="<<GetLastError() << std::endl;

            // remove write lock if any
            ov.Offset = 1;
            if (!UnlockFileEx(handle, 0, 1, /*hidword*/0, &ov)) {
                WinError winError("UnlockFileEx(handle=%1%)", handle/*, handleToPath(handle)*/);
                if (winError.lastError != ERROR_NOT_LOCKED)
                    throw winError;
            }
//          std::cerr << "/lockFile handle="<<handle << std::endl;
            return true;
        }
        case ltWrite: {
            OVERLAPPED ov = { 0 };
            ov.Offset = 1;
            if (!LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | (wait ? 0 : LOCKFILE_FAIL_IMMEDIATELY), 0, 1, 0, &ov)) {
                WinError winError("LockFileEx(handle=%1%)", handle/*, handleToPath(handle)*/);
//              std::cerr << "TODO: LockFileEx gle="<<winError.lastError << std::endl;
                if (winError.lastError == ERROR_LOCK_VIOLATION)
                    return false;
                throw winError;
            }
//          std::cerr << "LockFileEx ok? gle="<<GetLastError() << std::endl;

            // remove read lock if any
            ov.Offset = 0;
            if (!UnlockFileEx(handle, 0, 1, /*hidword*/0, &ov)) {
                WinError winError("UnlockFileEx(handle=%1%)", handle/*, handleToPath(handle)*/);
                if (winError.lastError != ERROR_NOT_LOCKED)
                    throw winError;
            }
//          std::cerr << "/lockFile handle="<<handle << std::endl;
            return true;
        }
        default: assert(false);
    }
}
#endif


PathLocks::PathLocks()
    : deletePaths(false)
{
}


PathLocks::PathLocks(const PathSet & paths, const string & waitMsg)
    : deletePaths(false)
{
    lockPaths(paths, waitMsg);
}


bool PathLocks::lockPaths(const PathSet & paths,
    const string & waitMsg, bool wait)
{
    std::string strpaths = "[";
    for(auto & v : paths) {
        if (strpaths.size() > 1)
            strpaths += ", ";
        strpaths += v;
    }
    strpaths += "]";
#ifdef _WIN32
    std::cerr << "lockPaths paths="<<strpaths<<" waitMsg="<<waitMsg<<" wait="<<wait << std::endl;
#endif
    assert(fds.empty());

    /* Note that `fds' is built incrementally so that the destructor
       will only release those locks that we have already acquired. */

    /* Acquire the lock for each path in sorted order. This ensures
       that locks are always acquired in the same order, thus
       preventing deadlocks. */
    for (auto & path : paths) {
        checkInterrupt();
        Path lockPath = path + ".lock";

        debug(format("locking path '%1%'") % path);

#ifndef _WIN32
        AutoCloseFD fd;
#else
        AutoCloseWindowsHandle fd;
#endif

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

                debug(format("lock acquired on '%1%'") % lockPath);

#ifndef _WIN32
                /* Check that the lock file hasn't become stale (i.e.,
                   hasn't been unlinked). */
                struct stat st;
                if (fstat(fd.get(), &st) == -1)
                    throw PosixError(format("statting lock file '%1%'") % lockPath);
                if (st.st_size != 0)
                    /* This lock file has been unlinked, so we're holding
                       a lock on a deleted file.  This means that other
                       processes may create and acquire a lock on
                       `lockPath', and proceed.  So we must retry. */
                    debug(format("open lock file '%1%' has become stale") % lockPath);
                else
#endif
                    break;
            }

            debug(format("lock acquired on '%1%'") % lockPath);

#ifndef _WIN32
            /* Check that the lock file hasn't become stale (i.e.,
               hasn't been unlinked). */
            struct stat st;
            if (fstat(fd.get(), &st) == -1)
                throw PosixError(format("statting lock file '%1%'") % lockPath);
            if (st.st_size != 0)
                /* This lock file has been unlinked, so we're holding
                   a lock on a deleted file.  This means that other
                   processes may create and acquire a lock on
                   `lockPath', and proceed.  So we must retry. */
                debug(format("open lock file '%1%' has become stale") % lockPath);
            else
#endif
                break;
        }

        /* Use borrow so that the descriptor isn't closed. */
        fds.push_back(FDPair(fd.release(), lockPath));
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

#ifndef _WIN32
        if (deletePaths) deleteLockFile(i.second, i.first);

        if (close(i.first) == -1)
            printError(
                format("error (ignored): cannot close lock file on '%1%'") % i.second);
#else
        if (!CloseHandle(i.first))
            printError(
                format("%3%:%4% error (ignored): cannot close lock file on '%1%' lastError=%d") % i.second % GetLastError() % __FILE__ % __LINE__);

        if (deletePaths) deleteLockFile(i.second); // close the file for delete to success
#endif

        debug(format("lock released on '%1%'") % i.second);
    }

    fds.clear();
}


void PathLocks::setDeletion(bool deletePaths)
{
    this->deletePaths = deletePaths;
}


}
