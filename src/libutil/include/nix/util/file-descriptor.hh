#pragma once
///@file

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
 * Read a file descriptor until EOF occurs.
 */
std::string drainFD(Descriptor fd, bool block = true, const size_t reserveSize = 0);

/**
 * The Windows version is always blocking.
 */
void drainFD(
    Descriptor fd,
    Sink & sink
#ifndef _WIN32
    ,
    bool block = true
#endif
);

/**
 * Read a symlink relative to a directory file descriptor.
 *
 * @throws SystemError on any I/O errors.
 * @throws Interrupted if interrupted.
 */
OsString readLinkAt(Descriptor dirFd, const CanonPath & path);

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
    void fsync() const;

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

#ifdef __linux__
namespace linux {

/**
 * Wrapper around Linux's openat2 syscall introduced in Linux 5.6.
 *
 * @see https://man7.org/linux/man-pages/man2/openat2.2.html
 * @see https://man7.org/linux/man-pages/man2/open_how.2type.html
v*
 * @param flags O_* flags
 * @param mode Mode for O_{CREAT,TMPFILE}
 * @param resolve RESOLVE_* flags
 *
 * @return nullopt if openat2 is not supported by the kernel.
 */
std::optional<Descriptor> openat2(Descriptor dirFd, const char * path, uint64_t flags, uint64_t mode, uint64_t resolve);

} // namespace linux
#endif

/**
 * Safe(r) function to open a file relative to dirFd, while
 * disallowing escaping from a directory and any symlinks in the process.
 *
 * @note On Windows, implemented via NtCreateFile single path component traversal
 * with FILE_OPEN_REPARSE_POINT. On Unix, uses RESOLVE_BENEATH with openat2 when
 * available, or falls back to openat single path component traversal.
 *
 * @param dirFd Directory handle to open relative to
 * @param path Relative path (no .. or . components)
 * @param desiredAccess (Windows) Windows ACCESS_MASK (e.g., GENERIC_READ, FILE_WRITE_DATA)
 * @param createOptions (Windows) Windows create options (e.g., FILE_NON_DIRECTORY_FILE)
 * @param createDisposition (Windows) FILE_OPEN, FILE_CREATE, etc.
 * @param flags (Unix) O_* flags
 * @param mode (Unix) Mode for O_{CREAT,TMPFILE}
 *
 * @pre path.isRoot() is false
 *
 * @throws SymlinkNotAllowed if any path components are symlinks
 * @throws SystemError on other errors
 */
Descriptor openFileEnsureBeneathNoSymlinks(
    Descriptor dirFd,
    const CanonPath & path,
#ifdef _WIN32
    ACCESS_MASK desiredAccess,
    ULONG createOptions,
    ULONG createDisposition = FILE_OPEN
#else
    int flags,
    mode_t mode = 0
#endif
);

#ifndef _WIN32
namespace unix {

/**
 * Try to change the mode of file named by \ref path relative to the parent directory denoted by \ref dirFd.
 *
 * @note When on linux without fchmodat2 support and without procfs mounted falls back to fchmodat without
 * AT_SYMLINK_NOFOLLOW, since it's the best we can do without failing.
 *
 * @pre path.isRoot() is false
 * @throws SysError if any operation fails
 */
void fchmodatTryNoFollow(Descriptor dirFd, const CanonPath & path, mode_t mode);

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
