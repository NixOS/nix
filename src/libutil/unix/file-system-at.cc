#include "nix/util/file-system-at.hh"
#include "nix/util/file-system.hh"
#include "nix/util/signals.hh"
#include "nix/util/source-accessor.hh"

#include <fcntl.h>
#include <unistd.h>

#if defined(__linux__)
#  include <sys/syscall.h> /* pull __NR_* definitions */
#endif

#if defined(__linux__) && defined(__NR_openat2)
#  define HAVE_OPENAT2 1
#  include <linux/openat2.h>
#else
#  define HAVE_OPENAT2 0
#endif

#if defined(__linux__) && defined(__NR_fchmodat2)
#  define HAVE_FCHMODAT2 1
#else
#  define HAVE_FCHMODAT2 0
#endif

#include "util-unix-config-private.hh"

namespace nix {

#ifdef __linux__

namespace linux {

std::optional<Descriptor> openat2(Descriptor dirFd, const char * path, uint64_t flags, uint64_t mode, uint64_t resolve)
{
#  if HAVE_OPENAT2
    /* Cache the result of whether openat2 is not supported. */
    static std::atomic_flag unsupported{};

    if (!unsupported.test()) {
        /* No glibc wrapper yet, but there's a patch:
         * https://patchwork.sourceware.org/project/glibc/patch/20251029200519.3203914-1-adhemerval.zanella@linaro.org/
         */
        auto how = ::open_how{.flags = flags, .mode = mode, .resolve = resolve};
        auto res = ::syscall(__NR_openat2, dirFd, path, &how, sizeof(how));
        /* Cache that the syscall is not supported. */
        if (res < 0 && errno == ENOSYS) {
            unsupported.test_and_set();
            return std::nullopt;
        }

        return res;
    }
#  endif
    return std::nullopt;
}

} // namespace linux

#endif

void unix::fchmodatTryNoFollow(Descriptor dirFd, const std::filesystem::path & path, mode_t mode)
{
    assert(path.is_relative());
    assert(!path.empty());

#if HAVE_FCHMODAT2
    /* Cache whether fchmodat2 is not supported. */
    static std::atomic_flag fchmodat2Unsupported{};
    if (!fchmodat2Unsupported.test()) {
        /* Try with fchmodat2 first. */
        auto res = ::syscall(__NR_fchmodat2, dirFd, path.c_str(), mode, AT_SYMLINK_NOFOLLOW);
        /* Cache that the syscall is not supported. */
        if (res < 0) {
            if (errno == ENOSYS)
                fchmodat2Unsupported.test_and_set();
            else {
                throw SysError([&] { return HintFmt("fchmodat2 %s", PathFmt(descriptorToPath(dirFd) / path)); });
            }
        } else
            return;
    }
#endif

#ifdef __linux__
    AutoCloseFD pathFd = ::openat(dirFd, path.c_str(), O_PATH | O_NOFOLLOW | O_CLOEXEC);
    if (!pathFd) {
        throw SysError([&] {
            return HintFmt(
                "opening %s to get an O_PATH file descriptor (fchmodat2 is unsupported)",
                PathFmt(descriptorToPath(dirFd) / path));
        });
    }

    /* Possible to use with O_PATH fd since
     * https://github.com/torvalds/linux/commit/55815f70147dcfa3ead5738fd56d3574e2e3c1c2 (3.6) */
    auto st = fstat(pathFd.get());

    if (S_ISLNK(st.st_mode))
        throw SysError(EOPNOTSUPP, "can't change mode of symlink %s", PathFmt(descriptorToPath(dirFd) / path));

    static std::atomic_flag dontHaveProc{};
    if (!dontHaveProc.test()) {
        static const std::filesystem::path selfProcFd = "/proc/self/fd";

        auto selfProcFdPath = selfProcFd / std::to_string(pathFd.get());
        if (int res = ::chmod(selfProcFdPath.c_str(), mode); res == -1) {
            if (errno == ENOENT)
                dontHaveProc.test_and_set();
            else {
                throw SysError([&] {
                    return HintFmt("chmod %s (%s)", PathFmt(selfProcFdPath), PathFmt(descriptorToPath(dirFd) / path));
                });
            }
        } else
            return;
    }

    static std::atomic<bool> warned = false;
    warnOnce(warned, "kernel doesn't support fchmodat2 and procfs isn't mounted, falling back to fchmodat");
#endif

    int res = ::fchmodat(
        dirFd,
        path.c_str(),
        mode,
#if defined(__APPLE__) || defined(__FreeBSD__)
        AT_SYMLINK_NOFOLLOW
#else
        0
#endif
    );

    if (res == -1) {
        throw SysError([&] { return HintFmt("fchmodat %s", PathFmt(descriptorToPath(dirFd) / path)); });
    }
}

static AutoCloseFD
openFileEnsureBeneathNoSymlinksIterative(Descriptor dirFd, const std::filesystem::path & path, int flags, mode_t mode)
{
    AutoCloseFD parentFd;
    auto components = std::vector<std::filesystem::path>(path.begin(), path.end());
    assert(!components.empty());
    auto getParentFd = [&]() { return parentFd ? parentFd.get() : dirFd; };

    /* This rather convoluted loop is necessary to avoid TOCTOU when validating that
       no inner path component is a symlink. */
    for (size_t i = 0; i + 1 < components.size(); ++i) {
        auto component = components[i].string();
        assert(component != ".." && !component.starts_with('/')); /* In case invariant is broken somehow.. */

        AutoCloseFD parentFd2 = ::openat(
            getParentFd(), /* First iteration uses dirFd. */
            component.c_str(),
            O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
#ifdef __linux__
                | O_PATH /* Linux-specific optimization. Files are open only for path resolution purposes. */
#endif
#ifdef __FreeBSD__
                | O_RESOLVE_BENEATH /* Further guard against any possible SNAFUs. */
#endif
        );

        if (!parentFd2) {
            /* Construct path up to failed component for error message. */
            std::filesystem::path path2;
            for (size_t j = 0; j <= i; ++j)
                path2 /= components[j];

            if (errno == ENOTDIR) /* Path component might be a symlink. */ {
                if (auto st = maybeFstatat(getParentFd(), component); st && S_ISLNK(st->st_mode))
                    throw SymlinkNotAllowed(path2);
                errno = ENOTDIR; /* Restore the errno. */
            } else if (errno == ELOOP) {
                throw SymlinkNotAllowed(path2);
            }

            return AutoCloseFD{};
        }

        parentFd = std::move(parentFd2);
    }

    AutoCloseFD res = ::openat(getParentFd(), components.back().c_str(), flags | O_NOFOLLOW, mode);
    if (!res && errno == ELOOP)
        throw SymlinkNotAllowed(path);
    return res;
}

AutoCloseFD
openFileEnsureBeneathNoSymlinks(Descriptor dirFd, const std::filesystem::path & path, int flags, mode_t mode)
{
    assert(path.is_relative());
    assert(!path.empty());
#if HAVE_OPENAT2
    auto maybeFd =
        linux::openat2(dirFd, path.c_str(), flags, static_cast<uint64_t>(mode), RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS);
    if (maybeFd) {
        if (*maybeFd < 0 && errno == ELOOP)
            throw SymlinkNotAllowed(path);
        return AutoCloseFD{*maybeFd};
    }
#endif
    return openFileEnsureBeneathNoSymlinksIterative(dirFd, path, flags, mode);
}

OsString readLinkAt(Descriptor dirFd, const std::filesystem::path & path)
{
    assert(path.is_relative());
    assert(!path.empty());
    std::vector<char> buf;
    for (ssize_t bufSize = PATH_MAX / 4; true; bufSize += bufSize / 2) {
        checkInterrupt();
        buf.resize(bufSize);
        ssize_t rlSize = ::readlinkat(dirFd, path.c_str(), buf.data(), bufSize);
        if (rlSize == -1) {
            throw SysError(
                [&] { return HintFmt("reading symbolic link %1%", PathFmt(descriptorToPath(dirFd) / path)); });
        } else if (rlSize < bufSize)
            return {buf.data(), static_cast<std::size_t>(rlSize)};
    }
}

static void symlinkAt(Descriptor dirFd, const std::filesystem::path & path, const OsString & target)
{
    assert(path.is_relative());
    assert(!path.empty());
    if (::symlinkat(target.c_str(), dirFd, path.c_str()) == -1) {
        throw SysError(
            [&] { return HintFmt("creating symlink %1% -> %2%", PathFmt(descriptorToPath(dirFd) / path), target); });
    }
}

void createFileSymlinkAt(Descriptor dirFd, const std::filesystem::path & path, const OsString & target)
{
    symlinkAt(dirFd, path, target);
}

void createDirectorySymlinkAt(Descriptor dirFd, const std::filesystem::path & path, const OsString & target)
{
    symlinkAt(dirFd, path, target);
}

void createUnknownSymlinkAt(Descriptor dirFd, const std::filesystem::path & path, const OsString & target)
{
    symlinkAt(dirFd, path, target);
}

outcome::unchecked<AutoCloseFD, std::error_code>
openDirectoryAt(Descriptor dirFd, const std::filesystem::path & path, bool create, mode_t mode)
{
    assert(path.is_relative());
    assert(!path.empty());
    if (create) {
        if (::mkdirat(dirFd, path.c_str(), mode) == -1) {
            return outcome::failure(std::error_code(errno, std::system_category()));
        }
    }
    int fd = ::openat(dirFd, path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd == -1) {
        return outcome::failure(std::error_code(errno, std::system_category()));
    }
    return AutoCloseFD{fd};
}

PosixStat fstat(Descriptor fd)
{
    PosixStat st;
    if (::fstat(fd, &st)) {
        throw SysError([&] { return HintFmt("getting status of %s", PathFmt(descriptorToPath(fd))); });
    }
    return st;
}

PosixStat fstatat(Descriptor dirFd, const std::filesystem::path & path)
{
    assert(path.is_relative());
    assert(!path.empty());
    PosixStat st;
    if (::fstatat(dirFd, path.c_str(), &st, AT_SYMLINK_NOFOLLOW)) {
        throw SysError([&] { return HintFmt("getting status of %s", PathFmt(descriptorToPath(dirFd) / path)); });
    }
    return st;
}

std::optional<PosixStat> maybeFstatat(Descriptor dirFd, const std::filesystem::path & path)
{
    assert(path.is_relative());
    assert(!path.empty());
    PosixStat st;
    if (::fstatat(dirFd, path.c_str(), &st, AT_SYMLINK_NOFOLLOW)) {
        if (errno == ENOENT || errno == ENOTDIR)
            return std::nullopt;
        throw SysError([&] { return HintFmt("getting status of %s", PathFmt(descriptorToPath(dirFd) / path)); });
    }
    return st;
}

} // namespace nix
