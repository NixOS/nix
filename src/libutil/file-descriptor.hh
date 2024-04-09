#pragma once
///@file

#include "types.hh"
#include "error.hh"

namespace nix {

struct Sink;
struct Source;

/**
 * Operating System capability
 */
typedef int Descriptor;

const Descriptor INVALID_DESCRIPTOR = -1;

/**
 * Convert a native `Descriptor` to a POSIX file descriptor
 *
 * This is a no-op except on Windows.
 */
static inline Descriptor toDescriptor(int fd)
{
    return fd;
}

/**
 * Convert a POSIX file descriptor to a native `Descriptor`
 *
 * This is a no-op except on Windows.
 */
static inline int fromDescriptor(Descriptor fd, int flags)
{
    return fd;
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

void drainFD(Descriptor fd, Sink & sink, bool block = true);

[[gnu::always_inline]]
inline Descriptor getStandardOut() {
    return STDOUT_FILENO;
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

/**
 * Close all file descriptors except those listed in the given set.
 * Good practice in child processes.
 */
void closeMostFDs(const std::set<Descriptor> & exceptions);

/**
 * Set the close-on-exec flag for the given file descriptor.
 */
void closeOnExec(Descriptor fd);

MakeError(EndOfFile, Error);

}
