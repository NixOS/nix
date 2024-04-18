#pragma once
///@file

#include "types.hh"
#include "error.hh"

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
#endif

namespace nix {

struct Sink;
struct Source;

/**
 * Operating System capability
 */
using Descriptor =
#if _WIN32
    HANDLE
#else
    int
#endif
    ;

const Descriptor INVALID_DESCRIPTOR =
#if _WIN32
    INVALID_HANDLE_VALUE
#else
    -1
#endif
    ;

/**
 * Convert a native `Descriptor` to a POSIX file descriptor
 *
 * This is a no-op except on Windows.
 */
static inline Descriptor toDescriptor(int fd)
{
#ifdef _WIN32
    return reinterpret_cast<HANDLE>(_get_osfhandle(fd));
#else
    return fd;
#endif
}

/**
 * Convert a POSIX file descriptor to a native `Descriptor` in read-only
 * mode.
 *
 * This is a no-op except on Windows.
 */
static inline int fromDescriptorReadOnly(Descriptor fd)
{
#ifdef _WIN32
    return _open_osfhandle(reinterpret_cast<intptr_t>(fd), _O_RDONLY);
#else
    return fd;
#endif
}

/**
 * Read the contents of a resource into a string.
 */
std::string readFile(Descriptor fd);

/**
 * Wrappers arount read()/write() that read/write exactly the
 * requested number of bytes.
 */
void readFull(Descriptor fd, char * buf, size_t count);

void writeFull(Descriptor fd, std::string_view s, bool allowInterrupts = true);

/**
 * Read a line from a file descriptor.
 */
std::string readLine(Descriptor fd);

/**
 * Write a line to a file descriptor.
 */
void writeLine(Descriptor fd, std::string s);

/**
 * Read a file descriptor until EOF occurs.
 */
std::string drainFD(Descriptor fd, bool block = true, const size_t reserveSize=0);

/**
 * The Windows version is always blocking.
 */
void drainFD(
      Descriptor fd
    , Sink & sink
#ifndef _WIN32
    , bool block = true
#endif
    );

[[gnu::always_inline]]
inline Descriptor getStandardOut() {
#ifndef _WIN32
    return STDOUT_FILENO;
#else
    return GetStdHandle(STD_OUTPUT_HANDLE);
#endif
}

/**
 * Automatic cleanup of resources.
 */
class AutoCloseFD
{
    Descriptor fd;
public:
    AutoCloseFD();
    AutoCloseFD(Descriptor fd);
    AutoCloseFD(const AutoCloseFD & fd) = delete;
    AutoCloseFD(AutoCloseFD&& fd);
    ~AutoCloseFD();
    AutoCloseFD& operator =(const AutoCloseFD & fd) = delete;
    AutoCloseFD& operator =(AutoCloseFD&& fd);
    Descriptor get() const;
    explicit operator bool() const;
    Descriptor release();
    void close();
    void fsync();
};

class Pipe
{
public:
    AutoCloseFD readSide, writeSide;
    void create();
    void close();
};

#ifndef _WIN32 // Not needed on Windows, where we don't fork

/**
 * Close all file descriptors except those listed in the given set.
 * Good practice in child processes.
 */
void closeMostFDs(const std::set<Descriptor> & exceptions);

/**
 * Set the close-on-exec flag for the given file descriptor.
 */
void closeOnExec(Descriptor fd);

#endif

#ifdef _WIN32
# if _WIN32_WINNT >= 0x0600
Path handleToPath(Descriptor handle);
std::wstring handleToFileName(Descriptor handle);
# endif
#endif

MakeError(EndOfFile, Error);

}
