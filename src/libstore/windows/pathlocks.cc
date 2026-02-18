#include "nix/util/logging.hh"
#include "nix/store/pathlocks.hh"
#include "nix/util/signals.hh"
#include "nix/util/util.hh"

#ifdef _WIN32
#  include <errhandlingapi.h>
#  include <fileapi.h>
#  include <windows.h>

namespace nix {

using namespace nix::windows;

void deleteLockFile(const std::filesystem::path & path, Descriptor desc)
{

    int exit = DeleteFileW(path.c_str());
    if (exit == 0)
        warn("%s: %s", PathFmt(path), std::to_string(GetLastError()));
}

void PathLocks::unlock()
{
    for (auto & i : fds) {
        if (deletePaths)
            deleteLockFile(i.second, i.first);

        if (CloseHandle(i.first) == -1)
            printError("error (ignored): cannot close lock file on %1%", PathFmt(i.second));

        debug("lock released on %1%", PathFmt(i.second));
    }

    fds.clear();
}

AutoCloseFD openLockFile(const std::filesystem::path & path, bool create)
{
    AutoCloseFD desc = CreateFileW(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        create ? OPEN_ALWAYS : OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_POSIX_SEMANTICS,
        NULL);
    if (desc.get() == INVALID_HANDLE_VALUE)
        warn("%s: %s", PathFmt(path), std::to_string(GetLastError()));

    return desc;
}

bool lockFile(Descriptor desc, LockType lockType, bool wait)
{
    switch (lockType) {
    case ltNone: {
        OVERLAPPED ov = {0};
        if (!UnlockFileEx(desc, 0, 2, 0, &ov))
            throw WinError("Failed to unlock file desc %s", desc);
        return true;
    }
    case ltRead: {
        OVERLAPPED ov = {0};
        if (!LockFileEx(desc, wait ? 0 : LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov)) {
            auto lastError = GetLastError();
            if (lastError == ERROR_LOCK_VIOLATION)
                return false;
            throw WinError(lastError, "Failed to lock file desc %s", desc);
        }

        ov.Offset = 1;
        if (!UnlockFileEx(desc, 0, 1, 0, &ov)) {
            auto lastError = GetLastError();
            if (lastError != ERROR_NOT_LOCKED)
                throw WinError(lastError, "Failed to unlock file desc %s", desc);
        }
        return true;
    }
    case ltWrite: {
        OVERLAPPED ov = {0};
        ov.Offset = 1;
        if (!LockFileEx(desc, LOCKFILE_EXCLUSIVE_LOCK | (wait ? 0 : LOCKFILE_FAIL_IMMEDIATELY), 0, 1, 0, &ov)) {
            auto lastError = GetLastError();
            if (lastError == ERROR_LOCK_VIOLATION)
                return false;
            throw WinError(lastError, "Failed to lock file desc %s", desc);
        }

        ov.Offset = 0;
        if (!UnlockFileEx(desc, 0, 1, 0, &ov)) {
            auto lastError = GetLastError();
            if (lastError != ERROR_NOT_LOCKED)
                throw WinError(lastError, "Failed to unlock file desc %s", desc);
        }
        return true;
    }
    default:
        assert(false);
    }
}

bool PathLocks::lockPaths(const std::set<std::filesystem::path> & paths, const std::string & waitMsg, bool wait)
{
    assert(fds.empty());

    for (auto & path : paths) {
        checkInterrupt();
        std::filesystem::path lockPath = path;
        lockPath += L".lock";
        debug("locking path %1%", PathFmt(path));

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

            debug("lock acquired on %1%", PathFmt(lockPath));

            if (getFileSize(fd.get()) != 0)
                debug("open lock file %1% has become stale", PathFmt(lockPath));
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

} // namespace nix
#endif
