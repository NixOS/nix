#include "nix/util/file-system-at.hh"
#include "nix/util/signals.hh"
#include "nix/util/util.hh"

#ifndef _WIN32
#  include <fcntl.h>
#endif

namespace nix {

static void recursiveSyncAt(Descriptor dirFd, const OsFilename & name, std::filesystem::file_type type)
{
    checkInterrupt();

    /* TODO: Fix on windows, FlushFileBuffers requires GENERIC_WRITE. */

    /* The directory read may not have told us the type. */
    if (type == std::filesystem::file_type::none) {
        auto st = fstatat(dirFd, name);
        type = S_ISREG(st.st_mode)   ? std::filesystem::file_type::regular
               : S_ISDIR(st.st_mode) ? std::filesystem::file_type::directory
                                     : std::filesystem::file_type::unknown;
    }

    NIX_INCOMPLETE_SWITCH_BEGIN
    switch (type) {
    case std::filesystem::file_type::regular: {
        auto fd = valueOr(
            openFileEnsureBeneathNoSymlinks(
                dirFd,
                name,
#ifdef _WIN32
                GENERIC_READ,
                0
#else
                O_RDONLY | O_CLOEXEC
#endif
                ),
            NIX_THROW_NATIVE_SYS_ERROR_HANDLER("opening file %s", PathFmt(descriptorToPath(dirFd) / name.path())));
        fd.fsync();
        break;
    }

    case std::filesystem::file_type::directory: {
        auto fd = valueOr(
            openFileEnsureBeneathNoSymlinks(
                dirFd,
                name,
#ifdef _WIN32
                FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                FILE_DIRECTORY_FILE
#else
                O_RDONLY | O_DIRECTORY | O_CLOEXEC
#endif
                ),
            NIX_THROW_NATIVE_SYS_ERROR_HANDLER("opening directory %s", PathFmt(descriptorToPath(dirFd) / name.path())));

        /* The iterator gets a duplicate, since it consumes the read
           position, while `fd` is only used for `*At` calls. */
        for (const auto & entry : DirectoryIterator{dupDescriptor(fd.get())})
            recursiveSyncAt(fd.get(), entry.name, entry.type);

        /* Only after its entries, so that the directory is not durable
           before what it contains. */
        fd.fsync();
        break;
    }

    default:
        /* Symlinks and other special files have no data of their own
           to flush. */
        break;
    }
    NIX_INCOMPLETE_SWITCH_END
}

void recursiveSyncAt(Descriptor dirFd, const OsFilename & name)
{
    recursiveSyncAt(dirFd, name, std::filesystem::file_type::none);
}

AutoDeleteAt::~AutoDeleteAt()
{
    try {
        deletePath();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

void AutoDeleteAt::deletePath()
{
    if (path) {
        deletePathAt(dirFd, *path);
        path.reset();
    }
}

} // namespace nix
