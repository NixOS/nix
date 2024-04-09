#include "file-system.hh"
#include "signals.hh"
#include "finally.hh"
#include "serialise.hh"

#include <fcntl.h>
#include <unistd.h>

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
    drainFD(fd, sink, block);
    return std::move(sink.s);
}


//////////////////////////////////////////////////////////////////////


AutoCloseFD::AutoCloseFD() : fd{INVALID_DESCRIPTOR} {}


AutoCloseFD::AutoCloseFD(Descriptor fd) : fd{fd} {}


AutoCloseFD::AutoCloseFD(AutoCloseFD && that) : fd{that.fd}
{
    that.fd = INVALID_DESCRIPTOR;
}


AutoCloseFD & AutoCloseFD::operator =(AutoCloseFD && that)
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
        ignoreException();
    }
}


Descriptor AutoCloseFD::get() const
{
    return fd;
}


void AutoCloseFD::close()
{
    if (fd != INVALID_DESCRIPTOR) {
        if(::close(fd) == -1)
            /* This should never happen. */
            throw SysError("closing file descriptor %1%", fd);
        fd = INVALID_DESCRIPTOR;
    }
}

void AutoCloseFD::fsync()
{
    if (fd != INVALID_DESCRIPTOR) {
        int result;
        result =
#if __APPLE__
            ::fcntl(fd, F_FULLFSYNC)
#else
            ::fsync(fd)
#endif
            ;
        if (result == -1)
            throw SysError("fsync file descriptor %1%", fd);
    }
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

}
