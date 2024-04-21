#include "logging.hh"
#include "pathlocks.hh"
#include "signals.hh"
#include "util.hh"
#include "pathlocks-impl.hh"
#include <errhandlingapi.h>
#include <fileapi.h>
namespace nix {



void PathLocks::unlock()
{
    warn("PathLocks::unlock: not yet implemented");
}


void deleteLockFile(const Path & path) {
  // https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-deletefilea
  int exit = DeleteFileA(path.c_str());
  if (exit == 0)
    warn("%s: &s", path, std::to_string(GetLastError()));

}

AutoCloseFD openLockFile(const Path & path, bool create)
{
  warn("PathLocks::openLockFile is experimental for Windows");
  // This should be in it's own conversion function for windows only
  std::wstring temp = std::wstring(path.begin(), path.end());
  LPCWSTR path_new = temp.c_str();
  AutoCloseFD handle = CreateFileW(path_new,
          GENERIC_READ | GENERIC_WRITE,
          FILE_SHARE_READ | FILE_SHARE_WRITE,
          NULL,
          create ? OPEN_ALWAYS : OPEN_EXISTING,
          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_POSIX_SEMANTICS,
          NULL);
  if (handle.get() == INVALID_HANDLE_VALUE)
      warn("%s: %s", path, std::to_string(GetLastError()));

  return handle;
}

bool lockFile(int fd, LockType lockType, bool wait) {
  return true;
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
        if (!lockFile(fd.get(), nix::unix::ltWrite, false)) {
            if (wait) {
                if (waitMsg != "") printError(waitMsg);
                lockFile(fd.get(), nix::unix::ltWrite, true);
            } else {
                unlock();
                return false;
            }
        }

        debug("lock aquired on '%1%'", lockPath);

        struct _stat st;
        // replace _fstat with proper impl
        if (_fstat(fd.get(), &st) == -1)
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

}
