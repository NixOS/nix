#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <tuple>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef __linux__
#  include <linux/capability.h>
#  include <sys/syscall.h>
#endif

#ifdef __FreeBSD__
#  include <sys/param.h>
#  include <sys/mount.h>
#endif

#include "nix/util/file-system.hh"
#include "nix/util/file-system-at.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/signals.hh"
#include "nix/util/util.hh"
#include "nix/util/system-error.hh"

#include "util-unix-config-private.hh"
#include "unix/unix-modes.hh"
#include "../file-descriptor-private.hh"

namespace nix {

struct DirectoryIterator::PlatformState
{
    AutoCloseDir dir;
};

DirectoryIterator::DirectoryIterator() noexcept = default;
DirectoryIterator::DirectoryIterator(DirectoryIterator &&) noexcept = default;
DirectoryIterator & DirectoryIterator::operator=(DirectoryIterator &&) noexcept = default;
DirectoryIterator::~DirectoryIterator() = default;

static std::filesystem::file_type fileTypeFromDirent(unsigned char dType)
{
    using T = std::filesystem::file_type;
    switch (dType) {
    case DT_REG:
        return T::regular;
    case DT_DIR:
        return T::directory;
    case DT_LNK:
        return T::symlink;
    case DT_CHR:
        return T::character;
    case DT_BLK:
        return T::block;
    case DT_FIFO:
        return T::fifo;
    case DT_SOCK:
        return T::socket;
    case DT_UNKNOWN:
        return T::none;
    default:
        return T::unknown;
    }
}

DirectoryIterator::DirectoryIterator(AutoCloseFD dirFd)
{
    auto s = std::make_unique<PlatformState>();
    s->dir = AutoCloseDir{::fdopendir(dirFd.get())};
    if (!s->dir)
        throw SysError("cannot read directory %s", PathFmt(descriptorToPath(dirFd.get())));
    /* Now owned by `s->dir`. */
    dirFd.release();
    state = std::move(s);
    ++*this;
}

DirectoryIterator & DirectoryIterator::operator++()
{
    auto & s = *state;
    while (true) {
        errno = 0;
        auto * dirent = ::readdir(s.dir.get());
        if (!dirent) {
            if (errno)
                throw SysError("reading directory %s", PathFmt(descriptorToPath(::dirfd(s.dir.get()))));
            state.reset();
            entry.reset();
            return *this;
        }
        std::string_view name = dirent->d_name;
        if (name == "." || name == "..")
            continue;
        entry = DirectoryEntry{
            .name = OsFilename::fromPathAsserting(name),
            .type = fileTypeFromDirent(dirent->d_type),
        };
        return *this;
    }
}

Result<AutoCloseFD, NativeError> openDirectory(const std::filesystem::path & path, FinalSymlink finalSymlink)
{
    return openResult(
        ::open(
            path.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | (finalSymlink == FinalSymlink::Follow ? 0 : O_NOFOLLOW)));
}

Result<AutoCloseFD, NativeError> openDirectoryTraversable(const std::filesystem::path & path, FinalSymlink finalSymlink)
{
    return openResult(
        ::open(
            path.c_str(),
            unix::traversableDirAccessMode | O_DIRECTORY | O_CLOEXEC
                | (finalSymlink == FinalSymlink::Follow ? 0 : O_NOFOLLOW)));
}

Result<AutoCloseFD, NativeError> openFileReadonly(const std::filesystem::path & path, FinalSymlink finalSymlink)
{
    return openResult(
        ::open(path.c_str(), O_RDONLY | O_CLOEXEC | (finalSymlink == FinalSymlink::Follow ? 0 : O_NOFOLLOW)));
}

Result<AutoCloseFD, NativeError>
openNewFileForWrite(const std::filesystem::path & path, mode_t mode, OpenNewFileForWriteParams params)
{
    auto flags = (params.writeOnly ? O_WRONLY : O_RDWR) | O_CREAT | O_CLOEXEC;
    if (params.truncateExisting) {
        flags |= O_TRUNC;
        if (!params.followSymlinksOnTruncate)
            flags |= O_NOFOLLOW;
    } else {
        flags |= O_EXCL; /* O_CREAT | O_EXCL already ensures that symlinks are not followed. */
    }
    return openResult(::open(path.c_str(), flags, mode));
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
    std::exception_ptr & ex,
    bool forceDontChmod MOUNTEDPATHS_PARAM)
{
    checkInterrupt();
#ifdef __FreeBSD__
    // In case of emergency (unmount fails for some reason) not recurse into mountpoints.
    // This prevents us from tearing up the nullfs-mounted nix store.
    if (mountedPaths.find(path) != mountedPaths.end()) {
        return;
    }
#endif

    auto name = OsFilename::fromPathThrowing(path.filename());

    auto st_ = maybeFstatat(parentfd, OsCanonPath{name});
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
        const auto PERM_MASK = S_IRUSR | S_IWUSR | S_IXUSR;
        const bool needsPermissionChange = !forceDontChmod && ((st.st_mode & PERM_MASK) != PERM_MASK);
        /* Make the directory accessible, but only if we really need to. */
        if (needsPermissionChange)
            try {
                unix::fchmodatTryNoFollow(parentfd, OsCanonPath{name}, st.st_mode | PERM_MASK);
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
            _deletePath(dirfd(dir.get()), path / childName, bytesFreed, ex, forceDontChmod MOUNTEDPATHS_ARG);
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

/**
 * Delete @p path, which is in the directory @p parentfd. @p path is
 * only used for messages and, on FreeBSD, to recognise mount points.
 */
static void deletePathIn(Descriptor parentfd, const std::filesystem::path & path, uint64_t & bytesFreed)
{
    bytesFreed = 0;
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

    std::exception_ptr ex;

    /* Whether directories don't need to be made writable/readable/traversable
       for them to be emptied. */
    bool forceDontChmod = false;

#ifdef __APPLE__
    /* Root can traverse and recursively remove unreadable, non-executable and
       non-writable directories on Darwin. App Management can deny changing a
       launched app bundle's mode even when deleting the bundle is allowed. */
    if (::geteuid() == 0)
        forceDontChmod = true;
#elif defined(__linux__)
    /* UID 0 alone does not guarantee permission to bypass directory modes.
       Query the calling thread, and keep chmod if the query fails. */
    __user_cap_header_struct header{.version = _LINUX_CAPABILITY_VERSION_3, .pid = 0};
    __user_cap_data_struct caps[_LINUX_CAPABILITY_U32S_3]{};
    if (::syscall(SYS_capget, &header, caps) == 0)
        forceDontChmod = caps[CAP_TO_INDEX(CAP_DAC_OVERRIDE)].effective & CAP_TO_MASK(CAP_DAC_OVERRIDE);
#endif

    _deletePath(parentfd, path, bytesFreed, ex, forceDontChmod MOUNTEDPATHS_ARG);

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
    assert(path.is_absolute());
    auto parentDirPath = path.parent_path();
    assert(parentDirPath != path);

    /* It's ok to follow symlinks in the parent since we only need to
       ensure that there's no TOCTOU when traversing inside the path. */
    auto dirfdOrError = openDirectory(parentDirPath, FinalSymlink::Follow);
    if (!dirfdOrError) {
        bytesFreed = 0;
        if (dirfdOrError.error() == ENOENT)
            return;
        throw SysError(dirfdOrError.error(), "opening directory %s", PathFmt(parentDirPath));
    }
    auto dirfd = std::move(dirfdOrError).value();

    deletePathIn(dirfd.get(), path, bytesFreed);
}

void deletePathAt(Descriptor dirFd, const OsCanonPath & path, uint64_t & bytesFreed)
{
    assert(!path.empty());

    Descriptor parentFd = dirFd;
    AutoCloseFD parentFdOwned;
    if (auto parent = path.parent(); !parent->empty()) {
        auto parentFdOrError = openFileEnsureBeneathNoSymlinks(dirFd, *parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (!parentFdOrError) {
            bytesFreed = 0;
            if (parentFdOrError.error() == ENOENT)
                return;
            throw SysError(
                parentFdOrError.error(), "opening directory %s", PathFmt(descriptorToPath(dirFd) / parent->path()));
        }
        parentFdOwned = std::move(parentFdOrError).value();
        parentFd = parentFdOwned.get();
    }

    deletePathIn(parentFd, descriptorToPath(dirFd) / path.path(), bytesFreed);
}

void deletePathAt(Descriptor dirFd, const OsCanonPath & path)
{
    uint64_t dummy;
    deletePathAt(dirFd, path, dummy);
}

void chown(const std::filesystem::path & path, uid_t owner, gid_t group)
{
    if (::chown(path.c_str(), owner, group) == -1)
        throw SysError("changing ownership of %s", PathFmt(path));
}

/**
 * Move @p srcName in @p srcParent to @p dstName in @p dstParent, like
 * `movePath`. @p srcPath and @p dstPath are only used for messages.
 */
static void movePathIn(
    Descriptor srcParent,
    const OsFilename & srcName,
    const std::filesystem::path & srcPath,
    Descriptor dstParent,
    const OsFilename & dstName,
    const std::filesystem::path & dstPath)
{
    /* If we have O_PATH (Linux, FreeBSD), then we can open the to-be-renamed
       file without requiring any permissions and "pin" the inode that we'll
       fchmod. This avoids operating on paths, which is prone to TOCTOU. */
#ifdef O_PATH
    AutoCloseFD fd = ::openat(srcParent, srcName.c_str(), O_NOFOLLOW | O_CLOEXEC | O_PATH);
    if (!fd)
        throw SysError("failed to open file %1%", PathFmt(srcPath));

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
    const PosixStat st = nix::fstatat(srcParent, srcName);
#endif

    bool changePerm = (::geteuid() && S_ISDIR(st.st_mode) && !(st.st_mode & S_IWUSR));
    if (changePerm) {
        const mode_t newMode = st.st_mode | S_IWUSR;
#ifdef O_PATH
        if (changePermsOnFile(newMode) == -1)
            throw SysError("making %s writable for renaming", PathFmt(srcPath));
#else
        unix::fchmodatTryNoFollow(srcParent, srcName, newMode);
#endif
    }

    if (::renameat(srcParent, srcName.c_str(), dstParent, dstName.c_str()) == -1) {
        const int savedErrno = errno;
        if (changePerm) {
            /* Ignore all errors on restoring permissions. We want to throw the
               original error originating from ::renameat. */
#ifdef O_PATH
            changePermsOnFile(st.st_mode);
#else
            try {
                unix::fchmodatTryNoFollow(srcParent, srcName, st.st_mode);
            } catch (SysError &) {
            }
#endif
        }
        throw SysError(savedErrno, "renaming %1% to %2%", PathFmt(srcPath), PathFmt(dstPath));
    }

    if (changePerm) {
#ifdef O_PATH
        if (changePermsOnFile(st.st_mode) == -1)
            throw SysError("restoring permissions on %s", PathFmt(dstPath));
#else
        unix::fchmodatTryNoFollow(dstParent, dstName, st.st_mode);
#endif
    }
}

/**
 * Open the parent directory of @p path, following symlinks, and
 * return it with the final component of @p path.
 */
static std::pair<AutoCloseFD, OsFilename> openParentOf(const std::filesystem::path & path)
{
    auto parentPath = path.parent_path();
    if (parentPath.empty())
        parentPath = ".";
    auto parentFd = valueOr(
        openDirectory(parentPath, FinalSymlink::Follow),
        NIX_THROW_NATIVE_SYS_ERROR_HANDLER("opening directory %s", PathFmt(parentPath)));
    return {std::move(parentFd), OsFilename::fromPathThrowing(path.filename())};
}

void movePath(const std::filesystem::path & src, const std::filesystem::path & dst)
{
    auto [srcParent, srcName] = openParentOf(src);
    auto [dstParent, dstName] = openParentOf(dst);
    movePathIn(srcParent.get(), srcName, src, dstParent.get(), dstName, dst);
}

/**
 * Open the parent directory of @p path beneath @p dirFd, without
 * following symlinks, for `*At` functions that operate on the final
 * component of @p path.
 *
 * @return The parent directory, owned if it had to be opened, and the
 * final component of @p path.
 */
static std::tuple<AutoCloseFD, Descriptor, OsFilename> openParentBeneath(Descriptor dirFd, const OsCanonPath & path)
{
    assert(!path.empty());
    auto lastIt = std::ranges::prev(path.end());
    OsFilename name = *lastIt;
    auto parent = path.parent();
    if (parent->empty())
        return {AutoCloseFD{}, dirFd, std::move(name)};
    auto parentFd = valueOr(
        openFileEnsureBeneathNoSymlinks(dirFd, *parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC),
        NIX_THROW_NATIVE_SYS_ERROR_HANDLER("opening directory %s", PathFmt(descriptorToPath(dirFd) / parent->path())));
    auto fd = parentFd.get();
    return {std::move(parentFd), fd, std::move(name)};
}

void movePathAt(Descriptor srcDirFd, const OsCanonPath & src, Descriptor dstDirFd, const OsCanonPath & dst)
{
    auto [srcParentOwned, srcParent, srcName] = openParentBeneath(srcDirFd, src);
    auto [dstParentOwned, dstParent, dstName] = openParentBeneath(dstDirFd, dst);
    movePathIn(
        srcParent,
        srcName,
        descriptorToPath(srcDirFd) / src.path(),
        dstParent,
        dstName,
        descriptorToPath(dstDirFd) / dst.path());
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
