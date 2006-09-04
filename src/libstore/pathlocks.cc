#include "pathlocks.hh"
#include "util.hh"

#include <cerrno>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef __CYGWIN__
#include <windows.h>
#include <sys/cygwin.h>
#endif


namespace nix {


int openLockFile(const Path & path, bool create)
{
    AutoCloseFD fd;

#ifdef __CYGWIN__
    /* On Cygwin we have to open the lock file without "DELETE"
       sharing mode; otherwise Windows will allow open lock files to
       be deleted (which is almost but not quite what Unix does). */
    char win32Path[MAX_PATH + 1];
    cygwin_conv_to_full_win32_path(path.c_str(), win32Path);

    SECURITY_ATTRIBUTES sa; /* required, otherwise inexplicably bad shit happens */
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = 0;
    sa.bInheritHandle = TRUE;
    HANDLE h = CreateFile(win32Path, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, 
        (create ? OPEN_ALWAYS : OPEN_EXISTING), 
        FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) {
        if (create || GetLastError() != ERROR_FILE_NOT_FOUND)
            throw Error(format("opening lock file `%1%'") % path);
        fd = -1;
    }
    else
        fd = cygwin_attach_handle_to_fd((char *) path.c_str(), -1, h, 1, O_RDWR);
#else        
    fd = open(path.c_str(), O_RDWR | (create ? O_CREAT : 0), 0666);
    if (fd == -1 && (create || errno != ENOENT))
        throw SysError(format("opening lock file `%1%'") % path);
#endif

    return fd.borrow();
}


void deleteLockFilePreClose(const Path & path, int fd)
{
#ifndef __CYGWIN__
    /* Get rid of the lock file.  Have to be careful not to introduce
       races. */
    /* On Unix, write a (meaningless) token to the file to indicate to
       other processes waiting on this lock that the lock is stale
       (deleted). */
    unlink(path.c_str());
    writeFull(fd, (const unsigned char *) "d", 1);
    /* Note that the result of unlink() is ignored; removing the lock
       file is an optimisation, not a necessity. */
#endif
}


void deleteLockFilePostClose(const Path & path)
{
#ifdef __CYGWIN__
    /* On Windows, just try to delete the lock file.  This will fail
       if anybody still has the file open.  We cannot use unlink()
       here, because Cygwin emulates Unix semantics of allowing an
       open file to be deleted (but fakes it - the file isn't actually
       deleted until later, so a file with the same name cannot be
       created in the meantime). */
    char win32Path[MAX_PATH + 1];
    cygwin_conv_to_full_win32_path(path.c_str(), win32Path);
    if (DeleteFile(win32Path))
        debug(format("delete of `%1%' succeeded") % path.c_str());
    else
        /* Not an error: probably means that the lock is still opened
           by someone else. */
        debug(format("delete of `%1%' failed: %2%") % path.c_str() % GetLastError());
#endif
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


void PathLocks::lockPaths(const PathSet & _paths, const string & waitMsg)
{
    /* May be called only once! */
    assert(fds.empty());
    
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

        AutoCloseFD fd;
        
        while (1) {

            /* Open/create the lock file. */
	    fd = openLockFile(lockPath, true);

            /* Acquire an exclusive lock. */
            if (!lockFile(fd, ltWrite, false)) {
                if (waitMsg != "") printMsg(lvlError, waitMsg);
                lockFile(fd, ltWrite, true);
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
}


PathLocks::~PathLocks()
{
    for (list<FDPair>::iterator i = fds.begin(); i != fds.end(); i++) {
        if (deletePaths) deleteLockFilePreClose(i->second, i->first);

        lockedPaths.erase(i->second);
        if (close(i->first) == -1)
            printMsg(lvlError,
                format("error (ignored): cannot close lock file on `%1%'") % i->second);

	if (deletePaths) deleteLockFilePostClose(i->second);

        debug(format("lock released on `%1%'") % i->second);
    }
}


void PathLocks::setDeletion(bool deletePaths)
{
    this->deletePaths = deletePaths;
}

 
}
