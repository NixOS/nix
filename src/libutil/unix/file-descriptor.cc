#include "file-system.hh"
#include "signals.hh"
#include "finally.hh"
#include "serialise.hh"

#include <fcntl.h>
#include <unistd.h>

namespace nix {

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
            if (errno == EINTR) continue;
            throw SysError("reading from file");
        }
        if (res == 0) throw EndOfFile("unexpected end-of-file");
        count -= res;
        buf += res;
    }
}


void writeFull(int fd, std::string_view s, bool allowInterrupts)
{
    while (!s.empty()) {
        if (allowInterrupts) checkInterrupt();
        ssize_t res = write(fd, s.data(), s.size());
        if (res == -1 && errno != EINTR)
            throw SysError("writing to file");
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
            if (errno != EINTR)
                throw SysError("reading a line");
        } else if (rd == 0) {
            if (eofOk)
                return s;
            else
                throw EndOfFile("unexpected EOF reading a line");
        }
        else {
            if (ch == '\n') return s;
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
        }
        else if (rd == 0) break;
        else sink({reinterpret_cast<char *>(buf.data()), (size_t) rd});
    }
}

//////////////////////////////////////////////////////////////////////

void Pipe::create()
{
    int fds[2];
#if HAVE_PIPE2
    if (pipe2(fds, O_CLOEXEC) != 0) throw SysError("creating pipe");
#else
    if (pipe(fds) != 0) throw SysError("creating pipe");
    unix::closeOnExec(fds[0]);
    unix::closeOnExec(fds[1]);
#endif
    readSide = fds[0];
    writeSide = fds[1];
}


//////////////////////////////////////////////////////////////////////

#if __linux__ || __FreeBSD__
static int unix_close_range(unsigned int first, unsigned int last, int flags)
{
#if !HAVE_CLOSE_RANGE
    return syscall(SYS_close_range, first, last, (unsigned int)flags);
#else
    return close_range(first, last, flags);
#endif
}
#endif

void unix::closeExtraFDs()
{
    constexpr int MAX_KEPT_FD = 2;
    static_assert(std::max({STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) == MAX_KEPT_FD);

#if __linux__ || __FreeBSD__
    // first try to close_range everything we don't care about. if this
    // returns an error with these parameters we're running on a kernel
    // that does not implement close_range (i.e. pre 5.9) and fall back
    // to the old method. we should remove that though, in some future.
    if (unix_close_range(MAX_KEPT_FD + 1, ~0U, 0) == 0) {
        return;
    }
#endif

#if __linux__
    try {
        for (auto & s : std::filesystem::directory_iterator{"/proc/self/fd"}) {
            checkInterrupt();
            auto fd = std::stoi(s.path().filename());
            if (fd > MAX_KEPT_FD) {
                debug("closing leaked FD %d", fd);
                close(fd);
            }
        }
        return;
    } catch (SysError &) {
    } catch (std::filesystem::filesystem_error &) {
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
    if ((prev = fcntl(fd, F_GETFD, 0)) == -1 ||
        fcntl(fd, F_SETFD, prev | FD_CLOEXEC) == -1)
        throw SysError("setting close-on-exec flag");
}

}
