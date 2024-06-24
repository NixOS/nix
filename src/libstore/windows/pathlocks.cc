#include "logging.hh"
#include "pathlocks.hh"
#include "signals.hh"
#include "util.hh"
#include <errhandlingapi.h>
#include <fileapi.h>
#include <windows.h>
#include "windows-error.hh"

namespace nix {

using namespace nix::windows;

void deleteLockFile(const Path & path, Descriptor desc)
{

    int exit = DeleteFileA(path.c_str());
    if (exit == 0)
        warn("%s: &s", path, std::to_string(GetLastError()));
}

void PathLocks::unlock()
{
    for (auto & i : fds) {
        if (deletePaths)
            deleteLockFile(i.second, i.first);

        if (CloseHandle(i.first) == -1)
            printError("error (ignored): cannot close lock file on '%1%'", i.second);

        debug("lock released on '%1%'", i.second);
    }

    fds.clear();
}

AutoCloseFD openLockFile(const Path & path, bool create)
{
    AutoCloseFD desc = CreateFileA(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        create ? OPEN_ALWAYS : OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_POSIX_SEMANTICS,
        NULL);
    if (desc.get() == INVALID_HANDLE_VALUE)
        warn("%s: %s", path, std::to_string(GetLastError()));

    return desc;
}

bool lockFile(Descriptor desc, LockType lockType, bool wait)
{
    switch (lockType) {
    case ltNone: {
        OVERLAPPED ov = {0};
        if (!UnlockFileEx(desc, 0, 2, 0, &ov)) {
            WinError winError("Failed to unlock file desc %s", desc);
            throw winError;
        }
        return true;
    }
    case ltRead: {
        OVERLAPPED ov = {0};
        if (!LockFileEx(desc, wait ? 0 : LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov)) {
            WinError winError("Failed to lock file desc %s", desc);
            if (winError.lastError == ERROR_LOCK_VIOLATION)
                return false;
            throw winError;
        }

        ov.Offset = 1;
        if (!UnlockFileEx(desc, 0, 1, 0, &ov)) {
            WinError winError("Failed to unlock file desc %s", desc);
            if (winError.lastError != ERROR_NOT_LOCKED)
                throw winError;
        }
        return true;
    }
    case ltWrite: {
        OVERLAPPED ov = {0};
        ov.Offset = 1;
        if (!LockFileEx(desc, LOCKFILE_EXCLUSIVE_LOCK | (wait ? 0 : LOCKFILE_FAIL_IMMEDIATELY), 0, 1, 0, &ov)) {
            WinError winError("Failed to lock file desc %s", desc);
            if (winError.lastError == ERROR_LOCK_VIOLATION)
                return false;
            throw winError;
        }

        ov.Offset = 0;
        if (!UnlockFileEx(desc, 0, 1, 0, &ov)) {
            WinError winError("Failed to unlock file desc %s", desc);
            if (winError.lastError != ERROR_NOT_LOCKED)
                throw winError;
        }
        return true;
    }
    default:
        assert(false);
    }
}

bool PathLocks::lockPaths(const PathSet & paths, const std::string & waitMsg, bool wait)
{
    assert(fds.empty());

    for (auto & path : paths) {
        checkInterrupt();
        Path lockPath = path + ".lock";
        debug("locking path '%1%'", path);

        AutoCloseFD fd;

        while (1) {
            fd = openLockFile(lockPath, true);
            if (!lockFile(fd.get(), ltWrite, false)) {
                if (wait) {
                    if (waitMsg != "")
                        printError(waitMsg);
                    lockFile(fd.get(), ltWrite, true);
                } else {
                    unlock();
                    return false;
                }
            }

            debug("lock aquired on '%1%'", lockPath);

            struct _stat st;
            if (_fstat(fromDescriptorReadOnly(fd.get()), &st) == -1)
                throw SysError("statting lock file '%1%'", lockPath);
            if (st.st_size != 0)
                debug("open lock file '%1%' has become stale", lockPath);
            else
                break;
        }

        fds.push_back(FDPair(fd.release(), lockPath));
    }
    return true;
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

}
