#include "nix/util/file-system.hh"
#include "nix/util/logging.hh"
#include "nix/store/pathlocks.hh"
#include "nix/util/signals.hh"
#include "nix/util/util.hh"
#include "nix/util/windows-environment.hh"

#include <errhandlingapi.h>
#include <fileapi.h>
#include <windows.h>

namespace nix {

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

/**
 * Release whatever lock @p desc holds, if any.
 *
 * `LockFileEx` locks taken by the same handle on the same range stack,
 * so keep going until there are none left.
 */
static void unlockAll(Descriptor desc)
{
    using namespace nix::windows;
    while (true) {
        OVERLAPPED ov = {0};
        if (!UnlockFileEx(desc, 0, 1, 0, &ov)) {
            auto lastError = GetLastError();
            /* Windows reports `ERROR_NOT_LOCKED` when there is nothing
               (left) to unlock, but Wine reports `ERROR_LOCK_VIOLATION`
               (`STATUS_FILE_LOCK_CONFLICT` from `unlock_fd` in its
               `server/fd.c`). */
            if (lastError == ERROR_NOT_LOCKED || (lastError == ERROR_LOCK_VIOLATION && isWine()))
                return;
            throw WinError(lastError, "unlocking %s", PathFmt(descriptorToPath(desc)));
        }
    }
}

bool lockFile(Descriptor desc, LockType lockType, bool wait)
{
    using namespace nix::windows;

    /* All locks are on the same single byte, so that shared and
       exclusive locks from different handles conflict, as they do with
       `flock` on Unix. Like `flock` (see flock(2)), changing the kind of
       lock a handle holds isn't atomic: the old one is released first. */
    unlockAll(desc);

    if (lockType == ltNone)
        return true;

    OVERLAPPED ov = {0};
    DWORD flags = (lockType == ltWrite ? LOCKFILE_EXCLUSIVE_LOCK : 0) | (wait ? 0 : LOCKFILE_FAIL_IMMEDIATELY);
    if (!LockFileEx(desc, flags, 0, 1, 0, &ov)) {
        auto lastError = GetLastError();
        if (lastError == ERROR_LOCK_VIOLATION)
            return false;
        throw WinError(lastError, "locking %s", PathFmt(descriptorToPath(desc)));
    }
    return true;
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
