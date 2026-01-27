#include "nix/util/canon-path.hh"
#include "nix/util/file-system.hh"
#include "nix/util/signals.hh"
#include "nix/util/finally.hh"
#include "nix/util/serialise.hh"
#include "nix/util/source-accessor.hh"

#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <span>

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

#include "util-config-private.hh"
#include "util-unix-config-private.hh"

namespace nix {

namespace {

// This function is needed to handle non-blocking reads/writes. This is needed in the buildhook, because
// somehow the json logger file descriptor ends up being non-blocking and breaks remote-building.
// TODO: get rid of buildhook and remove this function again (https://github.com/NixOS/nix/issues/12688)
void pollFD(int fd, int events)
{
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = events;
    int ret = poll(&pfd, 1, -1);
    if (ret == -1) {
        throw SysError("poll on file descriptor failed");
    }
}
} // namespace

std::make_unsigned_t<off_t> getFileSize(Descriptor fd)
{
    auto st = nix::fstat(fd);
    return st.st_size;
}

void readFull(int fd, char * buf, size_t count)
{
    while (count) {
        checkInterrupt();
        ssize_t res = ::read(fd, buf, count);
        if (res == -1) {
            switch (errno) {
            case EINTR:
                continue;
            case EAGAIN:
                pollFD(fd, POLLIN);
                continue;
            }
            throw SysError("reading from file");
        }
        if (res == 0)
            throw EndOfFile("unexpected end-of-file");
        count -= res;
        buf += res;
    }
}

void writeFull(int fd, std::string_view s, bool allowInterrupts)
{
    while (!s.empty()) {
        if (allowInterrupts)
            checkInterrupt();
        ssize_t res = write(fd, s.data(), s.size());
        if (res == -1) {
            switch (errno) {
            case EINTR:
                continue;
            case EAGAIN:
                pollFD(fd, POLLOUT);
                continue;
            }
            throw SysError("writing to file");
        }
        if (res > 0)
            s.remove_prefix(res);
    }
}

std::string readLine(int fd, bool eofOk, char terminator)
{
    std::string s;
    while (1) {
        checkInterrupt();
        char ch;
        // FIXME: inefficient
        ssize_t rd = ::read(fd, &ch, 1);
        if (rd == -1) {
            switch (errno) {
            case EINTR:
                continue;
            case EAGAIN: {
                pollFD(fd, POLLIN);
                continue;
            }
            default:
                throw SysError("reading a line");
            }
        } else if (rd == 0) {
            if (eofOk)
                return s;
            else
                throw EndOfFile("unexpected EOF reading a line");
        } else {
            if (ch == terminator)
                return s;
            s += ch;
        }
    }
}

size_t read(Descriptor fd, std::span<std::byte> buffer)
{
    ssize_t n = ::read(fd, buffer.data(), buffer.size());
    if (n == -1)
        throw SysError("read of %1% bytes", buffer.size());
    return static_cast<size_t>(n);
}

size_t readOffset(Descriptor fd, off_t offset, std::span<std::byte> buffer)
{
    ssize_t n = pread(fd, buffer.data(), buffer.size(), offset);
    if (n == -1)
        throw SysError("pread of %1% bytes at offset %2%", buffer.size(), offset);
    return static_cast<size_t>(n);
}

//////////////////////////////////////////////////////////////////////

void Pipe::create()
{
    int fds[2];
#if HAVE_PIPE2
    if (pipe2(fds, O_CLOEXEC) != 0)
        throw SysError("creating pipe");
#else
    if (pipe(fds) != 0)
        throw SysError("creating pipe");
    unix::closeOnExec(fds[0]);
    unix::closeOnExec(fds[1]);
#endif
    readSide = fds[0];
    writeSide = fds[1];
}

//////////////////////////////////////////////////////////////////////

#if defined(__linux__) || defined(__FreeBSD__)
static int unix_close_range(unsigned int first, unsigned int last, int flags)
{
#  if !HAVE_CLOSE_RANGE
    return syscall(SYS_close_range, first, last, (unsigned int) flags);
#  else
    return close_range(first, last, flags);
#  endif
}
#endif

void unix::closeExtraFDs()
{
    constexpr int MAX_KEPT_FD = 2;
    static_assert(std::max({STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) == MAX_KEPT_FD);

#if defined(__linux__) || defined(__FreeBSD__)
    // first try to close_range everything we don't care about. if this
    // returns an error with these parameters we're running on a kernel
    // that does not implement close_range (i.e. pre 5.9) and fall back
    // to the old method. we should remove that though, in some future.
    if (unix_close_range(MAX_KEPT_FD + 1, ~0U, 0) == 0) {
        return;
    }
#endif

#ifdef __linux__
    try {
        for (auto & s : DirectoryIterator{"/proc/self/fd"}) {
            checkInterrupt();
            auto fd = std::stoi(s.path().filename());
            if (fd > MAX_KEPT_FD) {
                debug("closing leaked FD %d", fd);
                close(fd);
            }
        }
        return;
    } catch (SysError &) {
    }
#endif

    int maxFD = 0;
#if HAVE_SYSCONF
    maxFD = sysconf(_SC_OPEN_MAX);
#endif
    for (int fd = MAX_KEPT_FD + 1; fd < maxFD; ++fd)
        close(fd); /* ignore result */
}

void unix::closeOnExec(int fd)
{
    int prev;
    if ((prev = fcntl(fd, F_GETFD, 0)) == -1 || fcntl(fd, F_SETFD, prev | FD_CLOEXEC) == -1)
        throw SysError("setting close-on-exec flag");
}

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

void unix::fchmodatTryNoFollow(Descriptor dirFd, const CanonPath & path, mode_t mode)
{
    assert(!path.isRoot());

#if HAVE_FCHMODAT2
    /* Cache whether fchmodat2 is not supported. */
    static std::atomic_flag fchmodat2Unsupported{};
    if (!fchmodat2Unsupported.test()) {
        /* Try with fchmodat2 first. */
        auto res = ::syscall(__NR_fchmodat2, dirFd, path.rel_c_str(), mode, AT_SYMLINK_NOFOLLOW);
        /* Cache that the syscall is not supported. */
        if (res < 0) {
            if (errno == ENOSYS)
                fchmodat2Unsupported.test_and_set();
            else
                throw SysError("fchmodat2 '%s' relative to parent directory", path.rel());
        } else
            return;
    }
#endif

#ifdef __linux__
    AutoCloseFD pathFd = ::openat(dirFd, path.rel_c_str(), O_PATH | O_NOFOLLOW | O_CLOEXEC);
    if (!pathFd)
        throw SysError(
            "opening '%s' relative to parent directory to get an O_PATH file descriptor (fchmodat2 is unsupported)",
            path.rel());

    struct ::stat st;
    /* Possible since https://github.com/torvalds/linux/commit/55815f70147dcfa3ead5738fd56d3574e2e3c1c2 (3.6) */
    if (::fstat(pathFd.get(), &st) == -1)
        throw SysError("statting '%s' relative to parent directory via O_PATH file descriptor", path.rel());

    if (S_ISLNK(st.st_mode))
        throw SysError(EOPNOTSUPP, "can't change mode of symlink '%s' relative to parent directory", path.rel());

    static std::atomic_flag dontHaveProc{};
    if (!dontHaveProc.test()) {
        static const CanonPath selfProcFd = CanonPath("/proc/self/fd");

        auto selfProcFdPath = selfProcFd / std::to_string(pathFd.get());
        if (int res = ::chmod(selfProcFdPath.c_str(), mode); res == -1) {
            if (errno == ENOENT)
                dontHaveProc.test_and_set();
            else
                throw SysError("chmod '%s' ('%s' relative to parent directory)", selfProcFdPath, path);
        } else
            return;
    }

    static std::atomic<bool> warned = false;
    warnOnce(warned, "kernel doesn't support fchmodat2 and procfs isn't mounted, falling back to fchmodat");
#endif

    int res = ::fchmodat(
        dirFd,
        path.rel_c_str(),
        mode,
#if defined(__APPLE__) || defined(__FreeBSD__)
        AT_SYMLINK_NOFOLLOW
#else
        0
#endif
    );

    if (res == -1)
        throw SysError("fchmodat '%s' relative to parent directory", path.rel());
}

static Descriptor
openFileEnsureBeneathNoSymlinksIterative(Descriptor dirFd, const CanonPath & path, int flags, mode_t mode)
{
    AutoCloseFD parentFd;
    auto nrComponents = std::ranges::distance(path);
    assert(nrComponents >= 1);
    auto components = std::views::take(path, nrComponents - 1); /* Everything but last component */
    auto getParentFd = [&]() { return parentFd ? parentFd.get() : dirFd; };

    /* This rather convoluted loop is necessary to avoid TOCTOU when validating that
       no inner path component is a symlink. */
    for (auto it = components.begin(); it != components.end(); ++it) {
        auto component = std::string(*it);                        /* Copy into a string to make NUL terminated. */
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
            /* Construct the CanonPath for error message. */
            auto path2 = std::ranges::fold_left(components.begin(), ++it, CanonPath::root, [](auto lhs, auto rhs) {
                lhs.push(rhs);
                return lhs;
            });

            if (errno == ENOTDIR) /* Path component might be a symlink. */ {
                struct ::stat st;
                if (::fstatat(getParentFd(), component.c_str(), &st, AT_SYMLINK_NOFOLLOW) == 0 && S_ISLNK(st.st_mode))
                    throw SymlinkNotAllowed(path2);
                errno = ENOTDIR; /* Restore the errno. */
            } else if (errno == ELOOP) {
                throw SymlinkNotAllowed(path2);
            }

            return INVALID_DESCRIPTOR;
        }

        parentFd = std::move(parentFd2);
    }

    auto res = ::openat(getParentFd(), std::string(path.baseName().value()).c_str(), flags | O_NOFOLLOW, mode);
    if (res < 0 && errno == ELOOP)
        throw SymlinkNotAllowed(path);
    return res;
}

Descriptor openFileEnsureBeneathNoSymlinks(Descriptor dirFd, const CanonPath & path, int flags, mode_t mode)
{
    assert(!path.rel().starts_with('/')); /* Just in case the invariant is somehow broken. */
    assert(!path.isRoot());
#if HAVE_OPENAT2
    auto maybeFd = linux::openat2(
        dirFd, path.rel_c_str(), flags, static_cast<uint64_t>(mode), RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS);
    if (maybeFd) {
        if (*maybeFd < 0 && errno == ELOOP)
            throw SymlinkNotAllowed(path);
        return *maybeFd;
    }
#endif
    return openFileEnsureBeneathNoSymlinksIterative(dirFd, path, flags, mode);
}

OsString readLinkAt(Descriptor dirFd, const CanonPath & path)
{
    assert(!path.isRoot());
    assert(!path.rel().starts_with('/')); /* Just in case the invariant is somehow broken. */
    std::vector<char> buf;
    for (ssize_t bufSize = PATH_MAX / 4; true; bufSize += bufSize / 2) {
        checkInterrupt();
        buf.resize(bufSize);
        ssize_t rlSize = ::readlinkat(dirFd, path.rel_c_str(), buf.data(), bufSize);
        if (rlSize == -1)
            throw SysError("reading symbolic link '%1%' relative to parent directory", path.rel());
        else if (rlSize < bufSize)
            return {buf.data(), static_cast<std::size_t>(rlSize)};
    }
}

void unix::sendMessageWithFds(Descriptor sockfd, std::string_view data, std::span<const int> fds)
{
    struct iovec iov{
        .iov_base = const_cast<char *>(data.data()),
        .iov_len = data.size(),
    };

    struct msghdr msg{
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = nullptr,
        .msg_controllen = 0,
    };

    auto cmsghdrAlign = std::align_val_t{alignof(struct cmsghdr)};

    auto deleteWrapper = [&](void * ptr) { ::operator delete(ptr, cmsghdrAlign); };

    // Allocate control message buffer with proper alignment for struct cmsghdr
    std::unique_ptr<void, decltype(deleteWrapper)> controlData(nullptr, deleteWrapper);

    if (!fds.empty()) {
        size_t controlSize = CMSG_SPACE(sizeof(int) * fds.size());
        controlData.reset(::operator new(controlSize, cmsghdrAlign));

        if (!controlData)
            throw SysError("allocating control message buffer");

        msg.msg_control = controlData.get();
        msg.msg_controllen = controlSize;

        auto * cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_len = CMSG_LEN(sizeof(int) * fds.size());
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;

        // Copy file descriptors. Duplicating them shouldn't be needed.
        auto * fdPtr = reinterpret_cast<int *>(CMSG_DATA(cmsg));
        for (size_t i = 0; i < fds.size(); ++i) {
            fdPtr[i] = fds[i];
        }
    }

    if (sendmsg(sockfd, &msg, 0) < 0)
        throw SysError("sendmsg");
}

} // namespace nix
