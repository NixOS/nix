#pragma once
/**
 * @file
 *
 * @brief File descriptor operations for almost arbitrary file
 * descriptors.
 *
 * More specialized file-system-specific operations are in
 * @ref file-system-at.hh.
 */

#include "nix/util/canon-path.hh"
#include "nix/util/error.hh"
#include "nix/util/os-string.hh"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace nix {

struct Sink;
struct Source;

/**
 * Operating System capability
 */
using Descriptor =
#ifdef _WIN32
    HANDLE
#else
    int
#endif
    ;

const Descriptor INVALID_DESCRIPTOR =
#ifdef _WIN32
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
 * Read the contents of a resource into a string.
 */
std::string readFile(Descriptor fd);

/**
 * Platform-specific read into a buffer.
 *
 * Thin wrapper around ::read (Unix) or ReadFile (Windows).
 * Handles EINTR on Unix. Treats ERROR_BROKEN_PIPE as EOF on Windows.
 *
 * @param fd The file descriptor to read from
 * @param buffer The buffer to read into
 * @return The number of bytes actually read (0 indicates EOF)
 * @throws SystemError on failure
 */
size_t read(Descriptor fd, std::span<std::byte> buffer);

/**
 * Platform-specific write from a buffer.
 *
 * Thin wrapper around ::write (Unix) or WriteFile (Windows).
 * Handles EINTR on Unix.
 *
 * @param fd The file descriptor to write to
 * @param buffer The buffer to write from
 * @return The number of bytes actually written
 * @throws SystemError on failure
 */
size_t write(Descriptor fd, std::span<const std::byte> buffer, bool allowInterrupts);

/**
 * Get the size of a file.
 *
 * Thin wrapper around fstat (Unix) or GetFileSizeEx (Windows).
 *
 * @param fd The file descriptor
 * @return The file size
 * @throws SystemError on failure
 */
std::make_unsigned_t<off_t> getFileSize(Descriptor fd);

/**
 * Platform-specific positioned read into a buffer.
 *
 * Thin wrapper around pread (Unix) or ReadFile with OVERLAPPED (Windows).
 * Does NOT handle EINTR on Unix - caller must catch and retry if needed.
 *
 * @param fd The file descriptor to read from (must be seekable)
 * @param offset The offset to read from
 * @param buffer The buffer to read into
 * @return The number of bytes actually read (0 indicates EOF)
 * @throws SystemError on failure
 */
size_t readOffset(Descriptor fd, off_t offset, std::span<std::byte> buffer);

/**
 * Read \ref nbytes starting at \ref offset from a seekable file into a sink.
 *
 * @throws SystemError if fd is not seekable or any operation fails
 * @throws Interrupted if the operation was interrupted
 * @throws EndOfFile if an EOF was reached before reading \ref nbytes
 */
void copyFdRange(Descriptor fd, off_t offset, size_t nbytes, Sink & sink);

/**
 * Wrappers around read()/write() that read/write exactly the
 * requested number of bytes.
 */
void readFull(Descriptor fd, char * buf, size_t count);

void writeFull(Descriptor fd, std::string_view s, bool allowInterrupts = true);

/**
 * Read a line from an unbuffered file descriptor.
 * See BufferedSource::readLine for a buffered variant.
 *
 * @param fd The file descriptor to read from
 * @param eofOk If true, return an unterminated line if EOF is reached. (e.g. the empty string)
 * @param terminator The chartacter that ends the line
 *
 * @return A line of text ending in `\n`, or a string without `\n` if `eofOk` is true and EOF is reached.
 */
std::string readLine(Descriptor fd, bool eofOk = false, char terminator = '\n');

/**
 * Write a line to a file descriptor.
 */
void writeLine(Descriptor fd, std::string s);

/**
 * Perform a blocking fsync operation on a file descriptor.
 */
void syncDescriptor(Descriptor fd);

/**
 * Options for draining a file descriptor to a sink.
 */
struct DrainFdSinkOpts
{
    /**
     * If provided, read exactly this many bytes (throws EndOfFile if EOF occurs before reading all bytes).
     */
    std::optional<std::make_unsigned_t<off_t>> expectedSize = {};

#ifndef _WIN32
    /**
     * Whether to block on read.
     */
    bool block = true;
#endif
};

/**
 * Options for draining a file descriptor to a string.
 */
struct DrainFdOpts
{
    /**
     * If expected=true: read exactly this many bytes (throws EndOfFile if EOF occurs before reading all bytes).
     * If expected=false: size hint for string allocation.
     */
    std::make_unsigned_t<off_t> size = 0;

    /**
     * If true, size is exact expected size. If false, size is just a reservation hint.
     */
    bool expected = false;

#ifndef _WIN32
    /**
     * Whether to block on read.
     */
    bool block = true;
#endif
};

/**
 * Read a file descriptor until EOF occurs.
 *
 * @param fd The file descriptor to drain
 * @param opts Options for the drain operation
 */
std::string drainFD(Descriptor fd, DrainFdOpts opts = {});

/**
 * Read a file descriptor until EOF occurs, writing to a sink.
 *
 * @param fd The file descriptor to drain
 * @param sink The sink to write data to
 * @param opts Options for the drain operation
 */
void drainFD(Descriptor fd, Sink & sink, DrainFdSinkOpts opts = {});

/**
 * Get [Standard Input](https://en.wikipedia.org/wiki/Standard_streams#Standard_input_(stdin))
 */
[[gnu::always_inline]]
inline Descriptor getStandardInput()
{
#ifndef _WIN32
    return STDIN_FILENO;
#else
    return GetStdHandle(STD_INPUT_HANDLE);
#endif
}

/**
 * Get [Standard Output](https://en.wikipedia.org/wiki/Standard_streams#Standard_output_(stdout))
 */
[[gnu::always_inline]]
inline Descriptor getStandardOutput()
{
#ifndef _WIN32
    return STDOUT_FILENO;
#else
    return GetStdHandle(STD_OUTPUT_HANDLE);
#endif
}

/**
 * Get [Standard Error](https://en.wikipedia.org/wiki/Standard_streams#Standard_error_(stderr))
 */
[[gnu::always_inline]]
inline Descriptor getStandardError()
{
#ifndef _WIN32
    return STDERR_FILENO;
#else
    return GetStdHandle(STD_ERROR_HANDLE);
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
    AutoCloseFD(AutoCloseFD && fd) noexcept;
    ~AutoCloseFD();
    AutoCloseFD & operator=(const AutoCloseFD & fd) = delete;
    AutoCloseFD & operator=(AutoCloseFD && fd);
    Descriptor get() const;
    explicit operator bool() const;
    Descriptor release();
    void close();

    /**
     * Perform a blocking fsync operation.
     */
    void fsync() const
    {
        if (fd != INVALID_DESCRIPTOR)
            nix::syncDescriptor(fd);
    }

    /**
     * Asynchronously flush to disk without blocking, if available on
     * the platform. This is just a performance optimization, and
     * fsync must be run later even if this is called.
     */
    void startFsync() const;
};

class Pipe
{
public:
    AutoCloseFD readSide, writeSide;
    void create();
    void close();
};

#ifndef _WIN32 // Not needed on Windows, where we don't fork
namespace unix {

/**
 * Close all file descriptors except stdio fds (ie 0, 1, 2).
 * Good practice in child processes.
 */
void closeExtraFDs();

/**
 * Set the close-on-exec flag for the given file descriptor.
 */
void closeOnExec(Descriptor fd);

} // namespace unix
#endif

MakeError(EndOfFile, Error);

#ifdef _WIN32

/**
 * Windows specific replacement for POSIX `lseek` that operates on a `HANDLE` and not
 * a file descriptor.
 */
off_t lseek(Descriptor fd, off_t offset, int whence);

#endif

} // namespace nix
