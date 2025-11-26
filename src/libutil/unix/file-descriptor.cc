#include "nix/util/canon-path.hh"
#include "nix/util/file-system.hh"
#include "nix/util/signals.hh"
#include "nix/util/finally.hh"
#include "nix/util/serialise.hh"

#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

#if defined(__linux__) && defined(__NR_openat2)
#  define HAVE_OPENAT2 1
#  include <sys/syscall.h>
#  include <linux/openat2.h>
#else
#  define HAVE_OPENAT2 0
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

std::string readFile(int fd)
{
    struct stat st;
    if (fstat(fd, &st) == -1)
        throw SysError("statting file");

    return drainFD(fd, true, st.st_size);
}

void readFull(int fd, char * buf, size_t count)
{
    while (count) {
        checkInterrupt();
        ssize_t res = read(fd, buf, count);
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

std::string readLine(int fd, bool eofOk)
{
    std::string s;
    while (1) {
        checkInterrupt();
        char ch;
        // FIXME: inefficient
        ssize_t rd = read(fd, &ch, 1);
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
            if (ch == '\n')
                return s;
            s += ch;
        }
    }
}

void drainFD(int fd, Sink & sink, bool block)
{
    // silence GCC maybe-uninitialized warning in finally
    int saved = 0;

    if (!block) {
        saved = fcntl(fd, F_GETFL);
        if (fcntl(fd, F_SETFL, saved | O_NONBLOCK) == -1)
            throw SysError("making file descriptor non-blocking");
    }

    Finally finally([&]() {
        if (!block) {
            if (fcntl(fd, F_SETFL, saved) == -1)
                throw SysError("making file descriptor blocking");
        }
    });

    std::vector<unsigned char> buf(64 * 1024);
    while (1) {
        checkInterrupt();
        ssize_t rd = read(fd, buf.data(), buf.size());
        if (rd == -1) {
            if (!block && (errno == EAGAIN || errno == EWOULDBLOCK))
                break;
            if (errno != EINTR)
                throw SysError("reading from file");
        } else if (rd == 0)
            break;
        else
            sink({reinterpret_cast<char *>(buf.data()), (size_t) rd});
    }
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
                    throw unix::SymlinkNotAllowed(path2);
                errno = ENOTDIR; /* Restore the errno. */
            } else if (errno == ELOOP) {
                throw unix::SymlinkNotAllowed(path2);
            }

            return INVALID_DESCRIPTOR;
        }

        parentFd = std::move(parentFd2);
    }

    auto res = ::openat(getParentFd(), std::string(path.baseName().value()).c_str(), flags | O_NOFOLLOW, mode);
    if (res < 0 && errno == ELOOP)
        throw unix::SymlinkNotAllowed(path);
    return res;
}

Descriptor unix::openFileEnsureBeneathNoSymlinks(Descriptor dirFd, const CanonPath & path, int flags, mode_t mode)
{
    assert(!path.rel().starts_with('/')); /* Just in case the invariant is somehow broken. */
    assert(!path.isRoot());
#ifdef __linux__
    auto maybeFd = linux::openat2(
        dirFd, path.rel_c_str(), flags, static_cast<uint64_t>(mode), RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS);
    if (maybeFd) {
        if (*maybeFd < 0 && errno == ELOOP)
            throw unix::SymlinkNotAllowed(path);
        return *maybeFd;
    }
#endif
    return openFileEnsureBeneathNoSymlinksIterative(dirFd, path, flags, mode);
}

} // namespace nix
