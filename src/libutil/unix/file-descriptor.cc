#include "nix/util/file-system.hh"
#include "nix/util/signals.hh"
#include "nix/util/finally.hh"
#include "nix/util/serialise.hh"

#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <span>

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
            auto savedErrno = errno;
            throw SysError(savedErrno, "reading from file %s", PathFmt(descriptorToPath(fd)));
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
            auto savedErrno = errno;
            throw SysError(savedErrno, "writing to file %s", PathFmt(descriptorToPath(fd)));
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
            default: {
                auto savedErrno = errno;
                throw SysError(savedErrno, "reading a line from %s", PathFmt(descriptorToPath(fd)));
            }
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

void syncDescriptor(Descriptor fd)
{
    int result =
#if defined(__APPLE__)
        ::fcntl(fd, F_FULLFSYNC)
#else
        ::fsync(fd)
#endif
        ;
    if (result == -1)
        throw NativeSysError("fsync file descriptor %1%", fd);
}

} // namespace nix
