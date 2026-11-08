#include "nix/store/pathlocks.hh"
#include "nix/util/util.hh"
#include "nix/util/file-system-at.hh"
#include "nix/util/system-error.hh"

#include <cerrno>
#include <cstdlib>

#ifndef _WIN32
#  include <fcntl.h>
#endif

namespace nix {

AutoCloseFD openLockFileAt(Descriptor dirFd, const OsFilename & name, bool create)
{
    auto fdOrError = openFileEnsureBeneathNoSymlinks(
        dirFd,
        name,
#ifdef _WIN32
        GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
        FILE_NON_DIRECTORY_FILE,
        create ? FILE_OPEN_IF : FILE_OPEN
#else
        O_CLOEXEC | O_RDWR | (create ? O_CREAT : 0),
        0600
#endif
    );
    if (!fdOrError) {
        if (!create && NativeSysError::toErrorCode(fdOrError.error()) == std::errc::no_such_file_or_directory)
            return {};
        throw NativeSysError(
            fdOrError.error(), "opening lock file %1%", PathFmt(descriptorToPath(dirFd) / name.path()));
    }
    return std::move(fdOrError).value();
}

PathLocks::PathLocks()
    : deletePaths(false)
{
}

PathLocks::PathLocks(const std::set<std::filesystem::path> & paths, const std::string & waitMsg)
    : deletePaths(false)
{
    lockPaths(paths, waitMsg);
}

PathLocks::~PathLocks()
{
    try {
        unlock();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

void PathLocks::setDeletion(bool deletePaths)
{
    this->deletePaths = deletePaths;
}

} // namespace nix
