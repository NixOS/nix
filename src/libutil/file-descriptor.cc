#include "file-system.hh"
#include "signals.hh"
#include "finally.hh"
#include "serialise.hh"

#include <fcntl.h>
#include <unistd.h>
#ifdef _WIN32
# include <winnt.h>
# include <fileapi.h>
# include "windows-error.hh"
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


AutoCloseFD::AutoCloseFD() : fd{Descriptor::invalid} {}


AutoCloseFD::AutoCloseFD(Descriptor fd) : fd{fd} {}


AutoCloseFD::AutoCloseFD(AutoCloseFD && that) : fd{that.fd}
{
    that.fd = Descriptor::invalid;
}


AutoCloseFD & AutoCloseFD::operator =(AutoCloseFD && that)
{
    close();
    fd = that.fd;
    that.fd = Descriptor::invalid;
    return *this;
}


AutoCloseFD::~AutoCloseFD()
{
    try {
        close();
    } catch (...) {
        ignoreException();
    }
}


Descriptor AutoCloseFD::get() const
{
    return fd;
}


void AutoCloseFD::close()
{
    if (fd != Descriptor::invalid) {
        if(
#ifdef _WIN32
           ::CloseHandle(fd)
#else
           ::close(fd)
#endif
           == -1)
            /* This should never happen. */
            throw NativeSysError("closing file descriptor %1%", fd);
        fd = Descriptor::invalid;
    }
}

void AutoCloseFD::fsync()
{
    if (fd != Descriptor::invalid) {
        int result;
        result =
#ifdef _WIN32
            ::FlushFileBuffers(fd)
#elif __APPLE__
            ::fcntl(fd, F_FULLFSYNC)
#else
            ::fsync(fd)
#endif
            ;
        if (result == -1)
            throw NativeSysError("fsync file descriptor %1%", fd);
    }
}


AutoCloseFD::operator bool() const
{
    return fd != Descriptor::invalid;
}


Descriptor AutoCloseFD::release()
{
    Descriptor oldFD = fd;
    fd = Descriptor::invalid;
    return oldFD;
}


//////////////////////////////////////////////////////////////////////


void Pipe::close()
{
    readSide.close();
    writeSide.close();
}

}
