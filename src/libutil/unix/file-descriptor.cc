#include "nix/util/file-system.hh"
#include "nix/util/file-system-at.hh"
#include "nix/util/signals.hh"

#include <fcntl.h>
#include <unistd.h>
#include <span>
#include <atomic>
#include <algorithm>

#include "util-unix-config-private.hh"
#include "../file-descriptor-private.hh"

namespace nix {

std::make_unsigned_t<off_t> getFileSize(Descriptor fd)
{
    auto st = nix::fstat(fd);
    return st.st_size;
}

size_t read(Descriptor fd, std::span<std::byte> buffer)
{
    ssize_t n;
    do {
        checkInterrupt();
        n = ::read(fd, buffer.data(), buffer.size());
    } while (n == -1 && errno == EINTR);
    if (n == -1)
        throw SysError("read of %1% bytes", buffer.size());
    return static_cast<size_t>(n);
}

size_t readOffset(Descriptor fd, off_t offset, std::span<std::byte> buffer)
{
    ssize_t n;
    do {
        checkInterrupt();
        n = ::pread(fd, buffer.data(), buffer.size(), offset);
    } while (n == -1 && errno == EINTR);
    if (n == -1)
        throw SysError("pread of %1% bytes at offset %2%", buffer.size(), offset);
    return static_cast<size_t>(n);
}

size_t write(Descriptor fd, std::span<const std::byte> buffer, bool allowInterrupts)
{
    ssize_t n;
    do {
        if (allowInterrupts)
            checkInterrupt();
        n = ::write(fd, buffer.data(), buffer.size());
    } while (n == -1 && errno == EINTR);
    if (n == -1)
        throw SysError("write of %1% bytes", buffer.size());
    return static_cast<size_t>(n);
}

AutoCloseFD dupDescriptor(Descriptor fd)
{
    int newFd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (newFd == -1)
        throw SysError("duplicating file descriptor");
    return AutoCloseFD{newFd};
}

//////////////////////////////////////////////////////////////////////

void Pipe::create(bool nonBlocking)
{
    int fds[2];
#if HAVE_PIPE2
    if (pipe2(fds, O_CLOEXEC | (nonBlocking ? O_NONBLOCK : 0)) != 0)
        throw SysError("creating pipe");
#else
    if (pipe(fds) != 0)
        throw SysError("creating pipe");
#endif
    readSide = fds[0];
    writeSide = fds[1];
#if !HAVE_PIPE2
    /* Assign the members *before* trying to make them non-blocking and
       close-on-exec since technically that can fail and we should still clean
       up those descriptors on destruction. Mostly pedantic exception safety, I
       can't envision a case this would fail on a freshly created pipe. */
    for (auto fd : fds) {
        unix::closeOnExec(fd);
        if (nonBlocking && ::fcntl(fd, F_SETFL, O_NONBLOCK) == -1)
            throw SysError("making pipe non-blocking");
    }
#endif
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

void unix::closeExtraFDs(std::span<const Descriptor> keep)
{
    constexpr int MAX_KEPT_FD = 2;
    static_assert(std::max({STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) == MAX_KEPT_FD);

    if (keep.empty()) {
        closeExtraFDs();
        return;
    }

    auto kept = [&](int fd) { return std::find(keep.begin(), keep.end(), fd) != keep.end(); };

    /* Everything strictly above the highest descriptor we are keeping can go
       in one sweep; the range below it has to be walked so the kept ones
       survive. */
    int maxKeep = MAX_KEPT_FD;
    for (auto fd : keep)
        maxKeep = std::max(maxKeep, static_cast<int>(fd));

    bool sweptAbove = false;
#if defined(__linux__) || defined(__FreeBSD__)
    sweptAbove = unix_close_range(maxKeep + 1, ~0U, 0) == 0;
#endif
    if (!sweptAbove) {
        int maxFD = 0;
#if HAVE_SYSCONF
        maxFD = sysconf(_SC_OPEN_MAX);
#endif
        for (int fd = maxKeep + 1; fd < maxFD; ++fd)
            close(fd); /* ignore result */
    }

    for (int fd = MAX_KEPT_FD + 1; fd <= maxKeep; ++fd)
        if (!kept(fd))
            close(fd); /* ignore result */
}

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

void unix::SelfPipe::create()
{
    pipe.create(/*nonBlocking=*/true);
}

void unix::SelfPipe::notify()
{
    /* Write to the self-pipe. If we get EAGAIN that means the notify pipe is full
       and we don't need to do anything. */
    ssize_t res;
    do {
        res = ::write(pipe.writeSide.get(), "x", 1);
    } while (res == -1 && errno == EINTR);
    if (res == -1 && errno != EAGAIN)
        throw SysError("writing to the self-pipe");
}

void unix::SelfPipe::drain()
{
    /* Drain the self-pipe. */
    std::array<char, 128> buf;
    while (true) {
        if (::read(pipe.readSide.get(), buf.data(), buf.size()) == -1) {
            if (errno == EAGAIN)
                break;
            else if (errno == EINTR)
                continue;
            else
                throw SysError("reading from self-pipe");
        }
    }
}

bool tryCopyFdRangeFast(Descriptor from, Descriptor to, off_t offset, size_t nbytes, size_t & written)
{
#if HAVE_COPY_FILE_RANGE

    /* Otherwise trying block cloning is pretty pointless. Also used to error
       out on partial copies (unless we happen to hit block alignment boundary,
       but that seems unlikely to be useful). */
    assert(written == 0);
    size_t left = nbytes;
    static std::atomic_flag copyFileRangeUnsupported = {};

    if (copyFileRangeUnsupported.test(std::memory_order_relaxed))
        return false;

    while (left) {
        checkInterrupt();

        ssize_t n = ::copy_file_range(
            from,
            &offset,
            to,
            nullptr,
            left,
            0
#  ifdef COPY_FILE_RANGE_CLONE /* FreeBSD */
                | COPY_FILE_RANGE_CLONE
#  endif
        );

        /* Reached EOF too early. */
        if (n == 0)
            throw EndOfFile(
                "unexpected end-of-file copying from %1% to %2%",
                PathFmt(descriptorToPath(from)),
                PathFmt(descriptorToPath(to)));

        if (n == -1) {
            if (errno == EINTR)
                continue; /* Loop over to hit checkInterrupt. */

            if (written == 0 && (errno == EINVAL || errno == EOPNOTSUPP || errno == EXDEV || errno == EBADF)) {
                /* Retry with fallback. */
            } else if (written == 0 && errno == ENOSYS) {
                /* Cache ENOSYS. */
                copyFileRangeUnsupported.test_and_set();
            } else {
                throw SysError([&] {
                    return HintFmt(
                        "copying contents of %1% to %2% via copy_file_range",
                        PathFmt(descriptorToPath(from)),
                        PathFmt(descriptorToPath(to)));
                });
            }

            return false;
        }

        assert(static_cast<uint64_t>(n) <= left);
        left -= n;
        written += n;
    }

    return true;
#else
    return false;
#endif
}

} // namespace nix
