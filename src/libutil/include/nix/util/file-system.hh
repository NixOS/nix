#pragma once
/**
 * @file
 *
 * @brief Utilities for working with the file system and file paths.
 *
 * Please try to use @ref file-system-at.hh instead of this where
 * possible, for the reasons given in the documentation of that header.
 */

#include "nix/util/fun.hh"
#include "nix/util/types.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/file-path.hh"
#include "nix/util/os-filename.hh"
#include "nix/util/deleter.hh"

#include <filesystem>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#ifdef _WIN32
#  include <windef.h>
#  include <wchar.h>
#endif

#include <functional>
#include <iterator>
#include <memory>
#include <optional>

/**
 * Polyfill for MinGW
 *
 * Windows does in fact support symlinks, but the C runtime interfaces predate this.
 * We define S_IFLNK and S_ISLNK so that our lstat implementation can properly
 * indicate symlinks by setting these mode bits when it detects a reparse point.
 */
#ifndef S_IFLNK
#  ifndef _WIN32
#    error "S_IFLNK should be defined on non-Windows platforms"
#  endif
#  define S_IFLNK 0120000
#endif
#ifndef S_ISLNK
#  ifndef _WIN32
#    error "S_ISLNK should be defined on non-Windows platforms"
#  endif
#  define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif

namespace nix {

struct Sink;
struct Source;

/**
 * @return An absolutized path, resolving paths relative to the
 * specified directory, or the current directory otherwise.  The path
 * is also canonicalised.
 *
 * In the process of being deprecated for `std::filesystem::absolute`.
 */
std::filesystem::path
absPath(const std::filesystem::path & path, const std::filesystem::path * dir = nullptr, bool resolveSymlinks = false);

/**
 * Canonicalise a path by removing all `.` or `..` components and
 * double or trailing slashes.  Optionally resolves all symlink
 * components such that each component of the resulting path is *not*
 * a symbolic link.
 *
 * In the process of being deprecated for
 * `std::filesystem::path::lexically_normal` (for the `resolveSymlinks =
 * false` case), and `std::filesystem::weakly_canonical` (for the
 * `resolveSymlinks = true` case).
 */
std::filesystem::path canonPath(const std::filesystem::path & path, bool resolveSymlinks = false);

/**
 * @return the base name of the given canonical path, i.e., everything
 * following the final `/` (trailing slashes are removed).
 *
 * In the process of being deprecated for
 * `std::filesystem::path::filename`.
 */
std::string_view baseNameOf(std::string_view path);

/**
 * Check whether 'path' is a descendant of 'dir'. Both paths must be
 * canonicalized.
 */
bool isInDir(const std::filesystem::path & path, const std::filesystem::path & dir);

/**
 * Check whether 'path' is equal to 'dir' or a descendant of
 * 'dir'. Both paths must be canonicalized.
 */
bool isDirOrInDir(const std::filesystem::path & path, const std::filesystem::path & dir);

/**
 * `struct stat` is not 64-bit everywhere on Windows.
 */
using PosixStat =
#ifdef _WIN32
    struct ::__stat64
#else
    struct ::stat
#endif
    ;

#ifdef _WIN32
namespace windows {

/**
 * Convert Windows FILETIME to Unix time_t.
 */
time_t fileTimeToUnixTime(const FILETIME & ft);

/**
 * Fill a PosixStat structure from file attributes and timestamps.
 *
 * @param dwFileAttributes File attributes (FILE_ATTRIBUTE_*)
 * @param ftCreationTime Creation time
 * @param ftLastAccessTime Last access time
 * @param ftLastWriteTime Last write time
 * @param nFileSizeHigh High 32 bits of file size
 * @param nFileSizeLow Low 32 bits of file size
 * @param nNumberOfLinks Number of hard links (default 1)
 */
void statFromFileInfo(
    PosixStat & st,
    DWORD dwFileAttributes,
    const FILETIME & ftCreationTime,
    const FILETIME & ftLastAccessTime,
    const FILETIME & ftLastWriteTime,
    DWORD nFileSizeHigh,
    DWORD nFileSizeLow,
    DWORD nNumberOfLinks = 1);

} // namespace windows
#endif

/**
 * Get status of `path`.
 */
PosixStat lstat(const std::filesystem::path & path);
/**
 * Get status of `path` following symlinks.
 */
PosixStat stat(const std::filesystem::path & path);
/**
 * `lstat` the given path if it exists.
 * @return std::nullopt if the path doesn't exist, or an optional containing the result of `lstat` otherwise
 */
std::optional<PosixStat> maybeLstat(const std::filesystem::path & path);
std::optional<PosixStat> maybeStat(const std::filesystem::path & path);

/**
 * @return true iff the given path exists.
 */
bool pathExists(const std::filesystem::path & path);

/**
 * A version of pathExists that returns false on a permission error.
 * Useful for inferring default paths across directories that might not
 * be readable.
 * @return true iff the given path can be accessed and exists
 */
bool pathAccessible(const std::filesystem::path & path);

/**
 * Read the contents (target) of a symbolic link.  The result is not
 * in any way canonicalised.
 */
std::filesystem::path readLink(const std::filesystem::path & path);

/**
 * Get the path associated with a file descriptor.
 *
 * @note One MUST only use this for error handling, because it creates
 * TOCTOU issues. We don't mind if error messages point to out of date
 * paths (that is a rather trivial TOCTOU --- the error message is best
 * effort) but for anything else we do.
 *
 * @note this function will clobber `errno` (Unix) / "last error"
 * (Windows), so care must be used to get those error codes, then call
 * this, then build a `SysError` / `WinError` with the saved error code.
 */
std::filesystem::path descriptorToPath(Descriptor fd);

/**
 * Type-safe boolean for whether to follow the final symlink component.
 */
enum struct FinalSymlink : bool { DontFollow = false, Follow = true };

/**
 * Open a `Descriptor` with read-only access to the given directory.
 *
 * @param finalSymlink If `DontFollow`, fail if the path is a symlink.
 *
 * @return The descriptor, or on failure the native error code.
 */
Result<AutoCloseFD, NativeError>
openDirectory(const std::filesystem::path & path, FinalSymlink finalSymlink = FinalSymlink::Follow);

/**
 * Open a `Descriptor` to the given directory only for traversing it:
 * using it as the base of `*at` calls, or `fstat`ing it.
 *
 * Unlike @ref nix::openDirectory(), this needs only search (traverse)
 * permission on the directory, not read (list) permission, where the OS
 * allows asking for that. In exchange, the descriptor may not support
 * listing the directory or `fsync`ing it.
 *
 * @param finalSymlink If `DontFollow`, fail if the path is a symlink.
 *
 * @return The descriptor, or on failure the native error code.
 */
Result<AutoCloseFD, NativeError>
openDirectoryTraversable(const std::filesystem::path & path, FinalSymlink finalSymlink = FinalSymlink::Follow);

/**
 * Open a `Descriptor` with read-only access to the given file.
 *
 * @note For directories use @ref nix::openDirectory().
 *
 * @return The descriptor, or on failure the native error code.
 */
Result<AutoCloseFD, NativeError>
openFileReadonly(const std::filesystem::path & path, FinalSymlink finalSymlink = FinalSymlink::Follow);

struct OpenNewFileForWriteParams
{
    /**
     * Whether to truncate an existing file.
     */
    bool truncateExisting:1 = false;
    /**
     * Whether to follow symlinks if @ref truncateExisting is true.
     */
    bool followSymlinksOnTruncate:1 = false;
    /**
     * Whether to open the newly created file as write-only.
     */
    bool writeOnly:1 = true;
};

/**
 * Open a `Descriptor` for write access or create it if it doesn't exist or truncate existing depending on
 * @ref nix::OpenNewFileForWriteParams::truncateExisting.
 *
 * @param mode POSIX permission bits. Ignored on Windows.
 * @return The descriptor, or on failure the native error code.
 *
 * @todo Reparse points on Windows.
 */
Result<AutoCloseFD, NativeError>
openNewFileForWrite(const std::filesystem::path & path, mode_t mode, OpenNewFileForWriteParams params);

/**
 * Read the contents of a file into a string.
 */
std::string readFile(const std::filesystem::path & path);
void readFile(const std::filesystem::path & path, Sink & sink, bool memory_map = true);

enum struct FsSync { Yes, No };

/**
 * Write a string to a file.
 */
void writeFile(
    const std::filesystem::path & path,
    std::string_view s,
    mode_t mode = 0666,
    FsSync sync = FsSync::No,
    FinalSymlink finalSymlink = FinalSymlink::Follow);

void writeFile(
    const std::filesystem::path & path,
    Source & source,
    mode_t mode = 0666,
    FsSync sync = FsSync::No,
    FinalSymlink finalSymlink = FinalSymlink::Follow);

void writeFile(Descriptor fd, Source & source, FsSync sync, const std::filesystem::path * origPath = nullptr);

void writeFile(
    Descriptor fd, std::string_view s, FsSync sync = FsSync::No, const std::filesystem::path * origPath = nullptr);

/**
 * Flush a path's parent directory to disk.
 */
void syncParent(const std::filesystem::path & path);

/**
 * Delete a path; i.e., in the case of a directory, it is deleted
 * recursively. It's not an error if the path does not exist. The
 * second variant returns the number of bytes and blocks freed.
 */
void deletePath(const std::filesystem::path & path);

/**
 * @param[out] bytesFreed Estimation of how many bytes of disk space have
 *                        been reclaimed. Initialised to zero internally, so
 *                        the caller doesn't need to initialise it.
 */
void deletePath(const std::filesystem::path & path, uint64_t & bytesFreed);

/**
 * Create a directory and all its parents, if necessary.
 *
 * Wrapper around `std::filesystem::create_directories` to handle exceptions.
 */
void createDirs(const std::filesystem::path & path);

/**
 * Create a single directory.
 */
void createDir(const std::filesystem::path & path, mode_t mode = 0755);

/**
 * Create a symlink.
 *
 */
void createSymlink(const std::filesystem::path & target, const std::filesystem::path & link);

/**
 * Atomically create or replace a symlink.
 */
void replaceSymlink(const std::filesystem::path & target, const std::filesystem::path & link);

/**
 * Recursively copy the content of `oldPath` to `newPath`.
 *
 * @todo Delete this. This function is prone to TOCTOU races and follows
 * symlinks in the destination.
 *
 * If contents is set, always create a regular file, even if the source is a
 * link.
 */
void copyFile(const std::filesystem::path & from, const std::filesystem::path & to, bool contents);

/**
 * Automatic cleanup of resources.
 */
class AutoDelete
{
    std::filesystem::path _path;
    bool del;
    bool recursive;
public:
    AutoDelete();

    AutoDelete(AutoDelete && x) noexcept
    {
        _path = std::move(x._path);
        del = x.del;
        recursive = x.recursive;
        x.del = false;
    }

    AutoDelete & operator=(AutoDelete && x) noexcept
    {
        swap(*this, x);
        return *this;
    }

    friend void swap(AutoDelete & lhs, AutoDelete & rhs) noexcept
    {
        using std::swap;
        swap(lhs._path, rhs._path);
        swap(lhs.del, rhs.del);
        swap(lhs.recursive, rhs.recursive);
    }

    AutoDelete(const std::filesystem::path & p, bool recursive = true);
    AutoDelete(const AutoDelete &) = delete;
    AutoDelete & operator=(const AutoDelete &) = delete;
    ~AutoDelete();

    /**
     * Delete the file the path points to, and cancel this `AutoDelete`,
     * so deletion is not attempted a second time by the destructor.
     *
     * The destructor calls this, but ignoring any exception.
     */
    void deletePath();

    /**
     * Cancel the pending deletion
     */
    void cancel() noexcept;

    const std::filesystem::path & path() const
    {
        return _path;
    }

    PathView view() const
    {
        return _path;
    }

    operator const std::filesystem::path &() const
    {
        return _path;
    }

    operator PathView() const
    {
        return _path;
    }
};

typedef std::unique_ptr<DIR, Deleter<closedir>> AutoCloseDir;

/**
 * Create a temporary directory.
 */
std::filesystem::path
createTempDir(const std::filesystem::path & tmpRoot = "", const std::string & prefix = "nix", mode_t mode = 0755);

/**
 * Create an anonymous readable/writable temporary file, returning a file handle.
 * On UNIX there resulting file isn't linked to any path on the filesystem.
 */
AutoCloseFD createAnonymousTempFile();

/**
 * Create a temporary file at a root, returning a file handle and its path.
 */
std::pair<AutoCloseFD, std::filesystem::path>
createTempFile(const std::filesystem::path & root, const std::filesystem::path & prefix);

/**
 * Create a temporary file, returning a file handle and its path.
 */
std::pair<AutoCloseFD, std::filesystem::path> createTempFile(const std::filesystem::path & prefix = "nix");

/**
 * Return `TMPDIR`, or the default temporary directory if unset or empty.
 * Uses GetTempPathW on windows which respects TMP, TEMP, USERPROFILE env variables.
 * Does not resolve symlinks and the returned path might not be directory or exist at all.
 */
std::filesystem::path defaultTempDir();

/**
 * Interpret `exe` as a location in the ambient file system and return
 * whether it resolves to a file that is executable.
 */
bool isExecutableFileAmbient(const std::filesystem::path & exe);

/**
 * Return temporary path constructed by appending a suffix to a root path.
 *
 * The constructed path looks like `<root><suffix>-<pid>-<unique>`. To create a
 * path nested in a directory, provide a suffix starting with `/`.
 */
std::filesystem::path makeTempPath(const std::filesystem::path & root, const std::string & suffix = ".tmp");

/**
 * Used in various places.
 *
 * @todo type
 */
typedef fun<bool(const std::string & path)> PathFilter;

extern PathFilter defaultPathFilter;

/**
 * Change permissions of a file only if necessary.
 *
 * @details
 * Skip chmod call if the directory already has the requested permissions.
 * This is to avoid failing when the executing user lacks permissions to change the
 * directory's permissions even if it would be no-op.
 *
 * @param path Path to the file to change the permissions for.
 * @param mode New file mode.
 * @param mask Used for checking if the file already has requested permissions.
 *
 * @return true if permissions changed, false otherwise.
 */
bool chmodIfNeeded(const std::filesystem::path & path, mode_t mode, mode_t mask = S_IRWXU | S_IRWXG | S_IRWXO);

/**
 * Set permissions on a path, throwing an exception on error.
 *
 * @param path Path to the file to change the permissions for.
 * @param mode New file mode.
 *
 * @todo stop using this and start using `fchmodatTryNoFollow` (or a different
 * wrapper) to avoid TOCTOU issues.
 */
void chmod(const std::filesystem::path & path, mode_t mode);

#ifndef _WIN32
/**
 * Change ownership of a file, throwing an exception on error.
 *
 * @param path Path to the file to change the ownership for.
 * @param owner New owner user ID.
 * @param group New owner group ID.
 */
void chown(const std::filesystem::path & path, uid_t owner, gid_t group);
#endif

/**
 * Remove a file, throwing an exception on error.
 *
 * @param path Path to the file to remove.
 */
void unlink(const std::filesystem::path & path);

/**
 * Remove a file, throwing an exception on error. ENOENT is ignored.
 *
 * @param path Path to the file to remove.
 */
void unlinkIfExists(const std::filesystem::path & path);

/**
 * Try to remove a file, ignoring errors.
 *
 * @param path Path to the file to try to remove.
 */
void tryUnlink(const std::filesystem::path & path);

/**
 * @brief Move/rename path 'src' to 'dst'.
 *
 * Temporarily makes 'src' writable if it's a directory and we're not root (to
 * be able to update the directory's parent link "..").
 *
 * Atomically replaces the destination if it exists. Does not follow symlinks if
 * dst is a symlink.
 */
void movePath(const std::filesystem::path & src, const std::filesystem::path & dst);

/**
 * @brief Thin wrapper around ::rename that throws SystemError on errors.
 *
 * Unlike @ref nix::movePath(), doesn't attempt to make the source writable if
 * it's a directory.
 */
void renameFile(const std::filesystem::path & src, const std::filesystem::path & dst);

/**
 * An entry read by @ref DirectoryIterator.
 */
struct DirectoryEntry
{
    OsFilename name;

    /**
     * The entry's own type (symlinks are not followed), as reported by
     * the directory read itself. `std::filesystem::file_type::none` if
     * it was not reported, in which case callers that need it have to
     * `lstat` the entry themselves.
     */
    std::filesystem::file_type type;
};

/**
 * @brief A directory iterator that can be used to iterate over the
 * contents of a directory, other than `.` and `..`. It is similar to
 * std::filesystem::directory_iterator but throws SystemError on failure
 * instead of std::filesystem::filesystem_error.
 *
 * Unlike `std::filesystem::directory_iterator`, it can only be moved,
 * not copied, since copies could only ever share one position, and the
 * entries it hands out are overwritten as it advances. It is its own
 * range: `begin()` moves it, so it can be used directly in a
 * range-based for loop. Also unlike `std::filesystem::directory_iterator`,
 * entries are just names relative to the directory rather than full
 * paths.
 */
class DirectoryIterator
{
public:
    // --- Iterator Traits ---
    using iterator_concept = std::input_iterator_tag;
    using value_type = DirectoryEntry;
    using difference_type = std::ptrdiff_t;
    using pointer = const DirectoryEntry *;
    using reference = const DirectoryEntry &;

    // Default constructor (represents end iterator)
    DirectoryIterator() noexcept;

    /**
     * Read the directory @p dirFd refers to, taking ownership of it.
     *
     * The handle's read position is consumed, so it must not be shared.
     * On Unix it must not be an `O_PATH` descriptor; on Windows it needs
     * `FILE_LIST_DIRECTORY` access.
     */
    explicit DirectoryIterator(AutoCloseFD dirFd);

    // Constructor taking a path
    explicit DirectoryIterator(const std::filesystem::path & path);

    DirectoryIterator(DirectoryIterator &&) noexcept;
    DirectoryIterator & operator=(DirectoryIterator &&) noexcept;
    ~DirectoryIterator();

    reference operator*() const
    {
        return *entry;
    }

    pointer operator->() const
    {
        return &*entry;
    }

    DirectoryIterator & operator++();

    // Postfix increment operator
    void operator++(int)
    {
        ++*this;
    }

    /**
     * Whether this is the end iterator.
     */
    bool operator==(std::default_sentinel_t) const noexcept
    {
        return !state;
    }

    // Allow direct use in range-based for loops if iterating over an instance
    DirectoryIterator begin()
    {
        return std::move(*this);
    }

    std::default_sentinel_t end() const noexcept
    {
        return std::default_sentinel;
    }

private:
    /**
     * The entry `operator*` hands out, overwritten as the iterator
     * advances.
     */
    std::optional<DirectoryEntry> entry;

    /**
     * Each platform's implementation defines this. Its destructor and
     * the iterator's moves are defined there too, where it is complete.
     */
    struct PlatformState;

    /**
     * Null exactly for the end iterator.
     */
    std::unique_ptr<PlatformState> state;
};

} // namespace nix
