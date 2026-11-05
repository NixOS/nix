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

#include "nix/util/error.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/file-system.hh"
#include "nix/util/os-canon-path.hh"
#include "nix/util/result.hh"
#include "nix/util/source-accessor.hh"

#include <optional>
#include <system_error>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#elif defined(__FreeBSD__)
// FreeBSD openat(..., AT_NOFOLLOW) on a symlink returns
// `EMLINK`, not the posix-specified `ELOOP`.
#  define NIX_ERR_OPEN_SYMLINK EMLINK
#else
#  define NIX_ERR_OPEN_SYMLINK ELOOP
#endif

namespace nix {

/**
 * Read a symlink relative to a directory file descriptor.
 * On Linux, this also supports reading from O_PATH descriptors with CanonPath::root.
 *
 * @throws SystemError on any I/O errors.
 * @throws Interrupted if interrupted.
 */
OsString readLinkAt(Descriptor dirFd, const OsCanonPath & path);

/**
 * Create a symlink to a file relative to a directory file descriptor.
 *
 * On Windows, creates a file symlink. On Unix, equivalent to symlinkat.
 *
 * @param dirFd Directory file descriptor
 * @param path Relative path for the new symlink
 * @param target The symlink target (what it points to)
 *
 * @throws SystemError on any I/O errors.
 */
void createFileSymlinkAt(Descriptor dirFd, const OsCanonPath & path, const OsString & target);

/**
 * Create a symlink to a directory relative to a directory file descriptor.
 *
 * On Windows, creates a directory symlink. On Unix, equivalent to symlinkat.
 *
 * @param dirFd Directory file descriptor
 * @param path Relative path for the new symlink
 * @param target The symlink target (what it points to)
 *
 * @throws SystemError on any I/O errors.
 */
void createDirectorySymlinkAt(Descriptor dirFd, const OsCanonPath & path, const OsString & target);

/**
 * Create a symlink relative to a directory file descriptor, detecting target type.
 *
 * On Windows, stats the target to determine whether to create a file or
 * directory symlink. Falls back to file symlink if the target does not exist.
 * On Unix, equivalent to symlinkat.
 *
 * @param dirFd Directory file descriptor
 * @param path Relative path for the new symlink
 * @param target The symlink target (what it points to)
 *
 * @throws SystemError on any I/O errors.
 */
void createUnknownSymlinkAt(Descriptor dirFd, const OsCanonPath & path, const OsString & target);

/**
 * Open a directory relative to a directory file descriptor.
 *
 * @param dirFd Directory file descriptor
 * @param path Relative path to the directory
 *
 * @return On success, the file descriptor for the directory. On failure, an
 * error code.
 *
 * @note Does not follow symlinks - if path is a symlink, will fail to open.
 */
Result<AutoCloseFD, std::error_code> openDirectoryAt(Descriptor dirFd, const OsCanonPath & path);

/**
 * Create a directory relative to a directory file descriptor, and (best
 * effort, see below) open it.
 *
 * @param dirFd Directory file descriptor
 * @param path Relative path to the new directory
 * @param mode File mode for the new directory.
 *             Unix only.
 *
 * @return On success, the file descriptor for the new directory if gotten
 * atomically, otherwise null (see below). On failure, an error code.
 *
 * @post @parblock
 * A file descriptor is only returned if it was opened atomically with the
 * creation of the directory it refers to. Opening the new directory is best
 * effort: if we cannot guarantee atomicity, we just return null, and a caller
 * that needs the directory opened has to open it itself, knowing that
 * something else could have replaced the directory in the meantime.
 *
 * Currently, atomically creating and opening is only possible on Windows.
 * Some Unixes, including (it seems) upcoming versions of Linux, could
 * also do this, with `O_CREAT | O_DIRECTORY | O_EXCL`. For Linux, see
 * https://lore.kernel.org/all/20261024213539.1307600-1-jkoolstra@xs4all.nl/
 * @endparblock
 */
Result<AutoCloseFD, std::error_code> createDirectoryAt(
    Descriptor dirFd,
    const OsCanonPath & path
#ifndef _WIN32
    ,
    mode_t mode = 0777
#endif
);

/**
 * Get status of an open file/directory handle.
 *
 * @param fd File descriptor/handle
 * @throws SystemError on I/O errors.
 */
PosixStat fstat(Descriptor fd);

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
std::optional<PosixStat> maybeFstatat(Descriptor dirFd, const OsCanonPath & path);

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
PosixStat fstatat(Descriptor dirFd, const OsCanonPath & path);

/**
 * Open a file relative to @p dirFd, ensuring the path stays beneath
 * @p dirFd and that no path component is a symlink (with the
 * exception that `O_PATH` (without `O_DIRECTORY`) on Unix permits a
 * trailing symlink).
 *
 * Callers must not pass `O_NOFOLLOW` on Unix (enforced by assert);
 * this function owns symlink policy and handles the flag internally.
 *
 * @param dirFdCallback Callback invoked that gets the ownership of an intermediate directory fd.
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
    Descriptor dirFd,         /** Directory handle to open relative to */
    const OsCanonPath & path, /** path Relative path (with no `..` or `.` components) */
#ifdef _WIN32
    ACCESS_MASK desiredAccess,           /** Windows `ACCESS_MASK` */
    ULONG createOptions,                 /** Windows create options */
    ULONG createDisposition = FILE_OPEN, /** `FILE_OPEN`, `FILE_CREATE`, etc. */
#else
    int flags,       /** `O_*` flags (must not include `O_NOFOLLOW`) */
    mode_t mode = 0, /** Mode for `O_{CREAT,TMPFILE}` */
#endif
    std::function<void(AutoCloseFD dirFd, OsCanonPath relPath)> dirFdCallback = nullptr);

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
 * Try to change the mode of file named by @p path relative to the parent directory denoted by @p dirFd.
 *
 * @note When on linux without fchmodat2 support and without procfs mounted falls back to fchmodat without
 * AT_SYMLINK_NOFOLLOW, since it's the best we can do without failing.
 *
 * @pre `path.empty()` is false
 * @throws SysError if any operation fails
 */
void fchmodatTryNoFollow(Descriptor dirFd, const OsCanonPath & path, mode_t mode);

} // namespace unix
#endif

} // namespace nix
