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
#include "nix/util/file-system.hh"
#include "nix/util/os-canon-path.hh"

#include <optional>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace nix {

/**
 * Get status of an open file/directory handle.
 *
 * @param fd File descriptor/handle
 * @throws SystemError on I/O errors.
 */
PosixStat fstat(Descriptor fd);

#ifndef _WIN32

/**
 * Get status of a file relative to a directory file descriptor.
 *
 * @param dirFd Directory file descriptor
 * @param path Relative path to stat
 *
 * @return nullopt if the path does not exist.
 * @throws SystemError on other I/O errors.
 *
 * @pre `path` must be relative (not absolute) and non-empty.
 */
std::optional<PosixStat> maybeFstatat(Descriptor dirFd, const std::filesystem::path & path);

/**
 * Get status of a file relative to a directory file descriptor.
 *
 * @param dirFd Directory file descriptor
 * @param path Relative path to stat
 *
 * @throws SystemError if the path does not exist or on other I/O errors.
 *
 * @pre `path` must be relative (not absolute) and non-empty.
 */
PosixStat fstatat(Descriptor dirFd, const std::filesystem::path & path);

#endif

/**
 * Read a symlink relative to a directory file descriptor.
 *
 * @throws SystemError on any I/O errors.
 * @throws Interrupted if interrupted.
 */
OsString readLinkAt(Descriptor dirFd, const OsCanonPath & path);

/**
 * Open a file relative to @p dirFd, ensuring the path stays beneath
 * @p dirFd and that no path component is a symlink (with the
 * exception that `O_PATH` (without `O_DIRECTORY`) on Unix permits a
 * trailing symlink).
 *
 * Callers must not pass `O_NOFOLLOW` on Unix (enforced by assert);
 * this function owns symlink policy and handles the flag internally.
 *
 * @param dirFd Directory handle to open relative to
 * @param path Relative path (with no `..` or `.` components)
 *
 * @param desiredAccess (Windows) Windows `ACCESS_MASK`
 * @param createOptions (Windows) Windows create options
 * @param createDisposition (Windows) `FILE_OPEN`, `FILE_CREATE`, etc.
 *
 * @param flags (Unix) `O_*` flags (must not include `O_NOFOLLOW`)
 * @param mode (Unix) Mode for `O_{CREAT,TMPFILE}`
 *
 * @pre `path.empty()` is false
 *
 * @throws SymlinkNotAllowed if an interior path component is a
 *     symlink, or if the final component is a symlink and `O_PATH`
 *     (without `O_DIRECTORY`) was *not* passed. With `O_PATH`
 *     (without `O_DIRECTORY`) on Unix, a trailing symlink is
 *     permitted and the caller receives a "path fd" to the symlink
 *     itself.
 *
 * @note With `O_CREAT | O_EXCL`, a pre-existing symlink at the
 *     final component causes the OS to return `EEXIST` rather
 *     than `ELOOP`, so `SymlinkNotAllowed` is *not* thrown — the
 *     caller sees a failed descriptor with `errno == EEXIST`.
 *
 * @return A valid descriptor on success, or an invalid descriptor
 *     on non-symlink errors (Unix: `errno` set, e.g. `ENOENT`,
 *     `ENOTDIR`, `EACCES`; Windows: last error set). The caller is
 *     responsible for checking the return value.
 *
 *     `errno` will never be `ELOOP` because that case is translated
 *     to a `SymlinkNotAllowed` throw instead.
 */
AutoCloseFD openFileEnsureBeneathNoSymlinks(
    Descriptor dirFd,
    const OsCanonPath & path,
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
std::optional<AutoCloseFD>
openat2(Descriptor dirFd, const char * path, uint64_t flags, uint64_t mode, uint64_t resolve);

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
void fchmodatTryNoFollow(Descriptor dirFd, const OsCanonPath & path, mode_t mode);

} // namespace unix
#endif

} // namespace nix
