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

AutoCloseFD openDirectory(const std::filesystem::path & path, FinalSymlink finalSymlink)
{
    return AutoCloseFD{open(
        path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | (finalSymlink == FinalSymlink::Follow ? 0 : O_NOFOLLOW))};
}

AutoCloseFD openFileReadonly(const std::filesystem::path & path)
{
    return AutoCloseFD{open(path.c_str(), O_RDONLY | O_CLOEXEC)};
}

AutoCloseFD openNewFileForWrite(const std::filesystem::path & path, mode_t mode, OpenNewFileForWriteParams params)
{
    auto flags = O_WRONLY | O_CREAT | O_CLOEXEC;
    if (params.truncateExisting) {
        flags |= O_TRUNC;
        if (!params.followSymlinksOnTruncate)
            flags |= O_NOFOLLOW;
    } else {
        flags |= O_EXCL; /* O_CREAT | O_EXCL already ensures that symlinks are not followed. */
    }
    return AutoCloseFD{open(path.c_str(), flags, mode)};
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
    } catch (SystemError &) {
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

std::optional<PosixStat> maybeLstat(const std::filesystem::path & path)
{
    std::optional<PosixStat> st{std::in_place};
    if (::lstat(path.c_str(), &*st)) {
        if (errno == ENOENT || errno == ENOTDIR)
            return std::nullopt;
        throw SysError("getting status of %s", PathFmt(path));
    }
    return st;
}

void setWriteTime(
    const std::filesystem::path & path, time_t accessedTime, time_t modificationTime, std::optional<bool> optIsSymlink)
{
    // Would be nice to use std::filesystem unconditionally, but
    // doesn't support access time just modification time.
    //
    // System clock vs File clock issues also make that annoying.
#if HAVE_UTIMENSAT && HAVE_DECL_AT_SYMLINK_NOFOLLOW
    struct timespec times[2] = {
        {
            .tv_sec = accessedTime,
            .tv_nsec = 0,
        },
        {
            .tv_sec = modificationTime,
            .tv_nsec = 0,
        },
    };
    if (utimensat(AT_FDCWD, path.c_str(), times, AT_SYMLINK_NOFOLLOW) == -1)
        throw SysError("changing modification time of %s (using `utimensat`)", PathFmt(path));
#else
    struct timeval times[2] = {
        {
            .tv_sec = accessedTime,
            .tv_usec = 0,
        },
        {
            .tv_sec = modificationTime,
            .tv_usec = 0,
        },
    };
#  if HAVE_LUTIMES
    if (lutimes(path.c_str(), times) == -1)
        throw SysError("changing modification time of %s", PathFmt{path});
#  else
    bool isSymlink = optIsSymlink ? *optIsSymlink : std::filesystem::is_symlink(path);

    if (!isSymlink) {
        if (utimes(path.c_str(), times) == -1)
            throw SysError("changing modification time of %s (not a symlink)", PathFmt{path});
    } else {
        throw Error("Cannot change modification time of symlink %s", PathFmt{path});
    }
#  endif
#endif
}

#ifdef __FreeBSD__
#  define MOUNTEDPATHS_PARAM , std::set<std::filesystem::path> & mountedPaths
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

    auto name = CanonPath::fromFilename(path.filename().native());

    PosixStat st;
    if (fstatat(parentfd, name.rel_c_str(), &st, AT_SYMLINK_NOFOLLOW) == -1) {
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
                unix::fchmodatTryNoFollow(parentfd, std::filesystem::path(name.rel()), st.st_mode | PERM_MASK);
            } catch (SysError & e) {
                e.addTrace({}, "while making directory %1% accessible for deletion", PathFmt(path));
                if (e.errNo == EOPNOTSUPP)
                    e.addTrace({}, "%1% is now a symlink, expected directory", PathFmt(path));
                throw;
            }

        int fd = openat(parentfd, name.rel_c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
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
    if (unlinkat(parentfd, name.rel_c_str(), flags) == -1) {
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
    auto parentDirPath = path.parent_path();
    assert(parentDirPath != path);

    /* It's ok to follow symlinks in the parent since we only need to
       ensure that there's no TOCTOU when traversing inside the path. */
    AutoCloseFD dirfd = openDirectory(parentDirPath, FinalSymlink::Follow);
    if (!dirfd) {
        if (errno == ENOENT)
            return;
        throw SysError("opening directory %s", PathFmt(parentDirPath));
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
    std::set<std::filesystem::path> mountedPaths;
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
