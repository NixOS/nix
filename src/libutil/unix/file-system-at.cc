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

std::optional<AutoCloseFD> openat2(Descriptor dirFd, const char * path, uint64_t flags, uint64_t mode, uint64_t resolve)
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

        return AutoCloseFD{static_cast<Descriptor>(res)};
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
#if defined(AT_SYMLINK_NOFOLLOW) && !defined(__linux__)
        AT_SYMLINK_NOFOLLOW
#else
        /* We would like to avoid following symlinks on Linux too. (Even though
           Linux doesn't support chmoding symlinks, we should still fail if we
           try, and not falsely succeed by following.) However, if we reach
           this point, rather than the Linux-specific cases above, it means we
           will likely hit glibc compat paths that will make using
           AT_SYMLINK_NOFOLLOW cause failures even if there is no symlink
           being followed! */
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
                /* Does not follow final symlink. We know `component` is a
                   single component so we don't have to worry about intermediate
                   symlinks either. */
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

    auto & lastComponent = components.back();
    AutoCloseFD res = ::openat(getParentFd(), lastComponent.c_str(), flags | O_NOFOLLOW, mode);

    if (!res) {
        if (errno == ELOOP)
            throw SymlinkNotAllowed(path);
        /* `O_DIRECTORY | O_NOFOLLOW` on a trailing symlink returns
           `ENOTDIR` rather than `ELOOP`. Post-check via `fstatat` to
           disambiguate — only on the error path, so the common
           successful-directory-open case pays no extra syscall. */
        if (errno == ENOTDIR) {
            if (auto st = maybeFstatat(getParentFd(), lastComponent); st && S_ISLNK(st->st_mode))
                throw SymlinkNotAllowed(path);
            /* Put back errno so the caller will get the original
               error. */
            errno = ENOTDIR;
        }
        return res;
    }

    /* For `O_PATH`, the defensive `| O_NOFOLLOW` we added above
       means a trailing symlink silently succeeds with an fd to the
       symlink itself (`O_PATH | O_NOFOLLOW` is the idiomatic way to
       obtain a symlink fd). This is intentional — `O_PATH` callers
       are asking for a path reference, and interior symlinks are
       already guarded by the component-by-component walk above. */


    return res;
}

AutoCloseFD
openFileEnsureBeneathNoSymlinks(Descriptor dirFd, const std::filesystem::path & path, int flags, mode_t mode)
{
    /* Just in case the invariant is somehow broken. */
    assert(path.is_relative());
    assert(!path.empty());

    /* We don't want callers of this function to think about the presence or
       absence of `O_NOFOLLOW`. "ensure beneath no symlinks" is in the name, so
       we want them to trust us to handle it instead. */
    assert(!(flags & O_NOFOLLOW));

    /* See doxygen in `file-system-at.hh` for why we reject this. */

#if HAVE_OPENAT2
    /* Two things are being fixed here:

       1. For `O_PATH` (without `O_DIRECTORY`), add `O_NOFOLLOW` so
          that a trailing symlink returns an fd to the symlink itself
          rather than `ELOOP`. `O_PATH | O_NOFOLLOW` is the idiomatic
          way to obtain a symlink fd, and `RESOLVE_NO_SYMLINKS` does
          not refuse it.

       2. We must not add `O_NOFOLLOW` when `O_DIRECTORY` is set,
          because `O_DIRECTORY | O_NOFOLLOW` on a trailing symlink
          returns `ENOTDIR` instead of `ELOOP`. Interior symlinks are
          still caught by `RESOLVE_NO_SYMLINKS` regardless, but the
          non-inclusion of `O_NOFOLLOW` is needed for
          `RESOLVE_NO_SYMLINKS` to make the final symlink an `ELOOP`
          rather than `O_DIRECTORY` making it an `ENOTDIR`.

       For other cases, `O_NOFOLLOW` doesn't really matter, but we
       default to not including it. */
    auto flagsAdj = (flags & O_PATH) && !(flags & O_DIRECTORY) ? flags | O_NOFOLLOW : flags;

    if (auto maybeFd = linux::openat2(
            dirFd, path.c_str(), flagsAdj, static_cast<uint64_t>(mode), RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS)) {
        if (!*maybeFd && errno == ELOOP)
            throw SymlinkNotAllowed(path);
        return std::move(*maybeFd);
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
    // Use O_NOFOLLOW to avoid following symlinks - if path is a symlink, this will fail with ENOTDIR
    int fd = ::openat(dirFd, path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
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
