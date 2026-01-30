#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef __FreeBSD__
#  include <sys/param.h>
#  include <sys/mount.h>
#endif

#include "nix/util/file-system.hh"
#include "nix/util/file-system-at.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/signals.hh"
#include "nix/util/util.hh"

#include "util-unix-config-private.hh"

namespace nix {

Descriptor openDirectory(const std::filesystem::path & path, bool followFinalSymlink)
{
    return open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | (followFinalSymlink ? 0 : O_NOFOLLOW));
}

Descriptor openFileReadonly(const std::filesystem::path & path)
{
    return open(path.c_str(), O_RDONLY | O_CLOEXEC);
}

Descriptor openNewFileForWrite(const std::filesystem::path & path, mode_t mode, OpenNewFileForWriteParams params)
{
    auto flags = O_WRONLY | O_CREAT | O_CLOEXEC;
    if (params.truncateExisting) {
        flags |= O_TRUNC;
        if (!params.followSymlinksOnTruncate)
            flags |= O_NOFOLLOW;
    } else {
        flags |= O_EXCL; /* O_CREAT | O_EXCL already ensures that symlinks are not followed. */
    }
    return open(path.c_str(), flags, mode);
}

std::filesystem::path descriptorToPath(Descriptor fd)
{
    if (fd == STDIN_FILENO)
        return "<stdin>";
    if (fd == STDOUT_FILENO)
        return "<stdout>";
    if (fd == STDERR_FILENO)
        return "<stderr>";

#if defined(__linux__)
    try {
        return readLink("/proc/self/fd/" + std::to_string(fd));
    } catch (...) {
    }
#elif HAVE_F_GETPATH
    /* F_GETPATH requires PATH_MAX buffer per POSIX */
    char buf[PATH_MAX];
    if (fcntl(fd, F_GETPATH, buf) != -1)
        return buf;
#endif

    /* Fallback for unknown fd or unsupported platform */
    return "<fd " + std::to_string(fd) + ">";
}

std::filesystem::path defaultTempDir()
{
    return getEnvNonEmpty("TMPDIR").value_or("/tmp");
}

PosixStat lstat(const std::filesystem::path & path)
{
    PosixStat st;
    if (::lstat(path.c_str(), &st))
        throw SysError("getting status of %s", PathFmt(path));
    return st;
}

#ifdef __FreeBSD__
#  define MOUNTEDPATHS_PARAM , std::set<Path> & mountedPaths
#  define MOUNTEDPATHS_ARG , mountedPaths
#else
#  define MOUNTEDPATHS_PARAM
#  define MOUNTEDPATHS_ARG
#endif

static void _deletePath(
    Descriptor parentfd,
    const std::filesystem::path & path,
    uint64_t & bytesFreed,
    std::exception_ptr & ex MOUNTEDPATHS_PARAM)
{
    checkInterrupt();
#ifdef __FreeBSD__
    // In case of emergency (unmount fails for some reason) not recurse into mountpoints.
    // This prevents us from tearing up the nullfs-mounted nix store.
    if (mountedPaths.find(path) != mountedPaths.end()) {
        return;
    }
#endif

    std::string name(path.filename());
    assert(name != "." && name != ".." && !name.empty());

    PosixStat st;
    if (fstatat(parentfd, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) == -1) {
        if (errno == ENOENT)
            return;
        throw SysError("getting status of %1%", PathFmt(path));
    }

    if (!S_ISDIR(st.st_mode)) {
        /* We are about to delete a file. Will it likely free space? */

        switch (st.st_nlink) {
        /* Yes: last link. */
        case 1:
            bytesFreed += st.st_size;
            break;
        /* Maybe: yes, if 'auto-optimise-store' or manual optimisation
           was performed. Instead of checking for real let's assume
           it's an optimised file and space will be freed.

           In worst case we will double count on freed space for files
           with exactly two hardlinks for unoptimised packages.
         */
        case 2:
            bytesFreed += st.st_size;
            break;
        /* No: 3+ links. */
        default:
            break;
        }
    }

    if (S_ISDIR(st.st_mode)) {
        /* Make the directory accessible. */
        const auto PERM_MASK = S_IRUSR | S_IWUSR | S_IXUSR;
        if ((st.st_mode & PERM_MASK) != PERM_MASK)
            try {
                unix::fchmodatTryNoFollow(parentfd, CanonPath(name), st.st_mode | PERM_MASK);
            } catch (SysError & e) {
                e.addTrace({}, "while making directory %1% accessible for deletion", PathFmt(path));
                if (e.errNo == EOPNOTSUPP)
                    e.addTrace({}, "%1% is now a symlink, expected directory", PathFmt(path));
                throw;
            }

        int fd = openat(parentfd, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (fd == -1)
            throw SysError("opening directory %1%", PathFmt(path));
        AutoCloseDir dir(fdopendir(fd));
        if (!dir)
            throw SysError("opening directory %1%", PathFmt(path));

        struct dirent * dirent;
        while (errno = 0, dirent = readdir(dir.get())) { /* sic */
            checkInterrupt();
            std::string childName = dirent->d_name;
            if (childName == "." || childName == "..")
                continue;
            _deletePath(dirfd(dir.get()), path / childName, bytesFreed, ex MOUNTEDPATHS_ARG);
        }
        if (errno)
            throw SysError("reading directory %1%", PathFmt(path));
    }

    int flags = S_ISDIR(st.st_mode) ? AT_REMOVEDIR : 0;
    if (unlinkat(parentfd, name.c_str(), flags) == -1) {
        if (errno == ENOENT)
            return;
        try {
            throw SysError("cannot unlink %1%", PathFmt(path));
        } catch (...) {
            if (!ex)
                ex = std::current_exception();
            else
                ignoreExceptionExceptInterrupt();
        }
    }
}

static void _deletePath(const std::filesystem::path & path, uint64_t & bytesFreed MOUNTEDPATHS_PARAM)
{
    assert(path.is_absolute());
    assert(path.parent_path() != path);

    AutoCloseFD dirfd = toDescriptor(open(path.parent_path().string().c_str(), O_RDONLY));
    if (!dirfd) {
        if (errno == ENOENT)
            return;
        throw SysError("opening directory %s", PathFmt(path.parent_path()));
    }

    std::exception_ptr ex;

    _deletePath(dirfd.get(), path, bytesFreed, ex MOUNTEDPATHS_ARG);

    if (ex)
        std::rethrow_exception(ex);
}

void deletePath(const std::filesystem::path & path)
{
    uint64_t dummy;
    deletePath(path, dummy);
}

void deletePath(const std::filesystem::path & path, uint64_t & bytesFreed)
{
    // Activity act(*logger, lvlDebug, "recursively deleting path '%1%'", path);
#ifdef __FreeBSD__
    std::set<Path> mountedPaths;
    struct statfs * mntbuf;
    int count;
    if ((count = getmntinfo(&mntbuf, MNT_WAIT)) < 0) {
        throw SysError("getmntinfo");
    }

    for (int i = 0; i < count; i++) {
        mountedPaths.emplace(mntbuf[i].f_mntonname);
    }
#endif
    bytesFreed = 0;
    _deletePath(path, bytesFreed MOUNTEDPATHS_ARG);
}

} // namespace nix
