#include "nix/util/serialise.hh"
#include "nix/util/util.hh"

#include <fcntl.h>
#include <unistd.h>
#ifdef _WIN32
#  include <winnt.h>
#  include <fileapi.h>
#  include "nix/util/windows-error.hh"
#endif

namespace nix {

void writeLine(Descriptor fd, std::string s)
{
    s += '\n';
    writeFull(fd, s);
}

std::string drainFD(Descriptor fd, bool block, const size_t reserveSize)
{
    // the parser needs two extra bytes to append terminating characters, other users will
    // not care very much about the extra memory.
    StringSink sink(reserveSize + 2);
#ifdef _WIN32
    // non-blocking is not supported this way on Windows
    assert(block);
    drainFD(fd, sink);
#else
    drainFD(fd, sink, block);
#endif
    return std::move(sink.s);
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

void AutoCloseFD::fsync() const
{
    if (fd != INVALID_DESCRIPTOR) {
        int result;
        result =
#ifdef _WIN32
            ::FlushFileBuffers(fd)
#elif defined(__APPLE__)
            ::fcntl(fd, F_FULLFSYNC)
#else
            ::fsync(fd)
#endif
            ;
        if (result == -1)
            throw NativeSysError("fsync file descriptor %1%", fd);
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
