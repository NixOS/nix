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

AutoCloseFD openFileReadonly(const std::filesystem::path & path, FinalSymlink finalSymlink)
{
    return AutoCloseFD{
        open(path.c_str(), O_RDONLY | O_CLOEXEC | (finalSymlink == FinalSymlink::Follow ? 0 : O_NOFOLLOW))};
}

AutoCloseFD openNewFileForWrite(const std::filesystem::path & path, mode_t mode, OpenNewFileForWriteParams params)
{
    auto flags = (params.writeOnly ? O_WRONLY : O_RDWR) | O_CREAT | O_CLOEXEC;
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
    return getEnvOsNonEmpty("TMPDIR").value_or("/tmp");
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

    auto name = OsFilename{path.filename()};

    auto st_ = maybeFstatat(parentfd, name.path());
    if (!st_)
        return;
    auto & st = *st_;

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
                unix::fchmodatTryNoFollow(parentfd, name, st.st_mode | PERM_MASK);
            } catch (SysError & e) {
                e.addTrace({}, "while making directory %1% accessible for deletion", PathFmt(path));
                if (e.errNo == EOPNOTSUPP)
                    e.addTrace({}, "%1% is now a symlink, expected directory", PathFmt(path));
                throw;
            }

        int fd = openat(parentfd, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
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
    auto parentDirPath = path.parent_path();
    assert(parentDirPath != path);

    AutoCloseFD dirfd = openDirectory(parentDirPath);
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

void chown(const std::filesystem::path & path, uid_t owner, gid_t group)
{
    if (::chown(path.c_str(), owner, group) == -1)
        throw SysError("changing ownership of %s", PathFmt(path));
}

void movePath(const std::filesystem::path & src, const std::filesystem::path & dst)
{
    /* If we have O_PATH (Linux, FreeBSD), then we can open the to-be-renamed
       file without requiring any permissions and "pin" the inode that we'll
       fchmod. This avoids operating on paths, which is prone to TOCTOU. */
#ifdef O_PATH
    AutoCloseFD fd = ::open(src.c_str(), O_NOFOLLOW | O_CLOEXEC | O_PATH);
    if (!fd)
        throw SysError("failed to open file %1%", PathFmt(src));

    auto changePermsOnFile = [&](mode_t newMode) -> int {
        /* AT_EMPTY_PATH here is supported since 6.6, but reports EINVAL without fchmodat2. */
        if (::fchmodat(fd.get(), "", newMode, AT_EMPTY_PATH) == 0)
            return 0;

#  ifdef __linux__
        if (errno == EINVAL) {
            auto path = fmt("/proc/self/fd/%d", fd.get());
            if (::chmod(path.c_str(), newMode) == 0)
                return 0;
        }
#  endif

        return -1;
    };

    const PosixStat st = nix::fstat(fd.get());
#else
    const PosixStat st = nix::lstat(src);
#endif

    bool changePerm = (::geteuid() && S_ISDIR(st.st_mode) && !(st.st_mode & S_IWUSR));
    if (changePerm) {
        const mode_t newMode = st.st_mode | S_IWUSR;
#ifdef O_PATH
        if (changePermsOnFile(newMode) == -1)
            throw SysError("making %s writable for renaming", PathFmt(src));
#else
        nix::chmod(src, newMode);
#endif
    }

    if (::rename(src.c_str(), dst.c_str()) == -1) {
        const int savedErrno = errno;
        if (changePerm) {
            /* Ignore all errors on restoring permissions. We want to throw the
               original error originating from ::rename. */
#ifdef O_PATH
            changePermsOnFile(st.st_mode);
#else
            ::chmod(src.c_str(), st.st_mode);
#endif
        }
        throw SysError(savedErrno, "renaming %1% to %2%", PathFmt(src), PathFmt(dst));
    }

    if (changePerm) {
#ifdef O_PATH
        if (changePermsOnFile(st.st_mode) == -1)
            throw SysError("restoring permissions on %s", PathFmt(dst));
#else
        nix::chmod(dst, st.st_mode);
#endif
    }
}

void renameFile(const std::filesystem::path & src, const std::filesystem::path & dst)
{
    if (::rename(src.c_str(), dst.c_str()) == -1)
        throw SysError("renaming %1% to %2%", PathFmt(src), PathFmt(dst));
}

void createDir(const std::filesystem::path & path, mode_t mode)
{
    if (::mkdir(path.c_str(), mode) == -1)
        throw SysError("creating directory %s", PathFmt(path));
}

} // namespace nix
