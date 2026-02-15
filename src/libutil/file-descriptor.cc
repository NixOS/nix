#include "nix/util/serialise.hh"
#include "nix/util/util.hh"
#include "nix/util/signals.hh"

#include <span>
#include <fcntl.h>
#include <unistd.h>
#ifdef _WIN32
#  include <winnt.h>
#  include <fileapi.h>
#endif

namespace nix {

void writeLine(Descriptor fd, std::string s)
{
    s += '\n';
    writeFull(fd, s);
}

std::string readFile(Descriptor fd)
{
    auto size = getFileSize(fd);
    // We can't rely on size being correct, most files in /proc have a nominal size of 0
    return drainFD(fd, {.size = size, .expected = false});
}

void drainFD(Descriptor fd, Sink & sink, DrainFdSinkOpts opts)
{
#ifndef _WIN32
    // silence GCC maybe-uninitialized warning in finally
    int saved = 0;

    if (!opts.block) {
        saved = fcntl(fd, F_GETFL);
        if (fcntl(fd, F_SETFL, saved | O_NONBLOCK) == -1)
            throw SysError("making file descriptor non-blocking");
    }

    Finally finally([&]() {
        if (!opts.block) {
            if (fcntl(fd, F_SETFL, saved) == -1)
                throw SysError("making file descriptor blocking");
        }
    });
#endif

    size_t bytesRead = 0;
    std::array<std::byte, 64 * 1024> buf;
    while (1) {
        checkInterrupt();

        size_t toRead = buf.size();
        if (opts.expectedSize) {
            size_t remaining = *opts.expectedSize - bytesRead;
            if (remaining == 0)
                break;
            toRead = std::min(toRead, remaining);
        }

        size_t n;
        try {
            n = read(fd, std::span(buf.data(), toRead));
        } catch (SystemError & e) {
#ifndef _WIN32
            if (!opts.block
                && (e.is(std::errc::resource_unavailable_try_again) || e.is(std::errc::operation_would_block)))
                break;
#endif
            if (e.is(std::errc::interrupted))
                continue;
            throw;
        }

        if (n == 0) {
            if (opts.expectedSize && bytesRead < *opts.expectedSize)
                throw EndOfFile("unexpected end-of-file");
            break;
        }

        bytesRead += n;
        sink(std::string_view(reinterpret_cast<const char *>(buf.data()), n));
    }
}

std::string drainFD(Descriptor fd, DrainFdOpts opts)
{
    // the parser needs two extra bytes to append terminating characters, other users will
    // not care very much about the extra memory.
    size_t reserveSize = opts.expected ? 0 : opts.size;
    StringSink sink(reserveSize + 2);
    DrainFdSinkOpts sinkOpts{
        .expectedSize = opts.expected ? std::optional<size_t>(opts.size) : std::nullopt,
#ifndef _WIN32
        .block = opts.block,
#endif
    };
    drainFD(fd, sink, sinkOpts);
    return std::move(sink.s);
}

void copyFdRange(Descriptor fd, off_t offset, size_t nbytes, Sink & sink)
{
    auto left = nbytes;
    std::array<std::byte, 64 * 1024> buf;

    while (left) {
        checkInterrupt();
        auto limit = std::min<size_t>(left, buf.size());
        /* Should be initialized before we read, because the `catch`
           block either throws or continues. */
        size_t n;
        try {
            n = readOffset(fd, offset, std::span(buf.data(), limit));
        } catch (SystemError & e) {
            if (e.is(std::errc::interrupted))
                continue;
            throw;
        }
        if (n == 0)
            throw EndOfFile("unexpected end-of-file");
        assert(n <= left);
        sink(std::string_view(reinterpret_cast<const char *>(buf.data()), n));
        offset += n;
        left -= n;
    }
}

//////////////////////////////////////////////////////////////////////

AutoCloseFD::AutoCloseFD()
    : fd{INVALID_DESCRIPTOR}
{
}

AutoCloseFD::AutoCloseFD(Descriptor fd)
    : fd{fd}
{
}

// NOTE: This can be noexcept since we are just copying a value and resetting
// the file descriptor in the rhs.
AutoCloseFD::AutoCloseFD(AutoCloseFD && that) noexcept
    : fd{that.fd}
{
    that.fd = INVALID_DESCRIPTOR;
}

AutoCloseFD & AutoCloseFD::operator=(AutoCloseFD && that)
{
    close();
    fd = that.fd;
    that.fd = INVALID_DESCRIPTOR;
    return *this;
}

AutoCloseFD::~AutoCloseFD()
{
    try {
        close();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

Descriptor AutoCloseFD::get() const
{
    return fd;
}

void AutoCloseFD::close()
{
    if (fd != INVALID_DESCRIPTOR) {
        if (
#ifdef _WIN32
            ::CloseHandle(fd)
#else
            ::close(fd)
#endif
            == -1)
            /* This should never happen. */
            throw NativeSysError("closing file descriptor %1%", fd);
        fd = INVALID_DESCRIPTOR;
    }
}

void AutoCloseFD::startFsync() const
{
#ifdef __linux__
    if (fd != -1) {
        /* Ignore failure, since fsync must be run later anyway. This is just a performance optimization. */
        ::sync_file_range(fd, 0, 0, SYNC_FILE_RANGE_WRITE);
    }
#endif
}

AutoCloseFD::operator bool() const
{
    return fd != INVALID_DESCRIPTOR;
}

Descriptor AutoCloseFD::release()
{
    Descriptor oldFD = fd;
    fd = INVALID_DESCRIPTOR;
    return oldFD;
}

//////////////////////////////////////////////////////////////////////

void Pipe::close()
{
    readSide.close();
    writeSide.close();
}

} // namespace nix
