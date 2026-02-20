#pragma once
/**
 * @file
 *
 * @brief File system operations relative to directory file descriptors.
 *
 * This header provides cross-platform wrappers for POSIX `*at` functions
 * (e.g., `symlinkat`, `mkdirat`, `readlinkat`) that operate relative to
 * a directory file descriptor.
 *
 * Prefer this to @ref file-system.hh because file descriptor-based file
 * system operations are necessary to avoid
 * [TOCTOU](https://en.wikipedia.org/wiki/Time-of-check_to_time-of-use)
 * issues.
 */

#include "nix/util/file-descriptor.hh"

#include <optional>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace nix {

/**
 * Read a symlink relative to a directory file descriptor.
 *
 * @throws SystemError on any I/O errors.
 * @throws Interrupted if interrupted.
 */
OsString readLinkAt(Descriptor dirFd, const CanonPath & path);

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
AutoCloseFD openFileEnsureBeneathNoSymlinks(
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

#ifdef __linux__
namespace linux {

/**
 * Wrapper around Linux's openat2 syscall introduced in Linux 5.6.
 *
 * @see https://man7.org/linux/man-pages/man2/openat2.2.html
 * @see https://man7.org/linux/man-pages/man2/open_how.2type.html
 *
 * @param flags O_* flags
 * @param mode Mode for O_{CREAT,TMPFILE}
 * @param resolve RESOLVE_* flags
 *
 * @return nullopt if openat2 is not supported by the kernel.
 */
std::optional<Descriptor> openat2(Descriptor dirFd, const char * path, uint64_t flags, uint64_t mode, uint64_t resolve);

} // namespace linux
#endif

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

} // namespace nix
