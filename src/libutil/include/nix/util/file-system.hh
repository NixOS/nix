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
#include <optional>

/**
 * Polyfill for MinGW
 *
 * Windows does in fact support symlinks, but the C runtime interfaces predate this.
 *
 * @todo get rid of this, and stop using `stat` when we want `lstat` too.
 */
#ifndef S_ISLNK
#  define S_ISLNK(m) false
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

/**
 * Get status of `path`.
 */
PosixStat lstat(const std::filesystem::path & path);
/**
 * Get status of `path` following symlinks.
 */
PosixStat stat(const std::filesystem::path & path);
/**
 * Get status of an open file descriptor.
 */
PosixStat fstat(int fd);
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
 * Canonicalize a path except for the last component.
 *
 * This is useful for getting the canonical location of a symlink.
 *
 * Consider the case where `foo/l` is a symlink. `canonical("foo/l")` will
 * resolve the symlink `l` to its target.
 * `makeParentCanonical("foo/l")` will not resolve the symlink `l` to its target,
 * but does ensure that the returned parent part of the path, `foo` is resolved
 * to `canonical("foo")`, and can therefore be retrieved without traversing any
 * symlinks.
 *
 * If a relative path is passed, it will be made absolute, so that the parent
 * can always be canonicalized.
 */
std::filesystem::path makeParentCanonical(const std::filesystem::path & path);

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
 * Open a `Descriptor` with read-only access to the given directory.
 */
AutoCloseFD openDirectory(const std::filesystem::path & path);

/**
 * Open a `Descriptor` with read-only access to the given file.
 *
 * @note For directories use @ref openDirectory.
 */
AutoCloseFD openFileReadonly(const std::filesystem::path & path);

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
};

/**
 * Open a `Descriptor` for write access or create it if it doesn't exist or truncate existing depending on @ref
 * truncateExisting.
 *
 * @param mode POSIX permission bits. Ignored on Windows.
 * @throws Nothing.
 *
 * @todo Reparse points on Windows.
 */
AutoCloseFD openNewFileForWrite(const std::filesystem::path & path, mode_t mode, OpenNewFileForWriteParams params);

/**
 * Read the contents of a file into a string.
 */
std::string readFile(const std::filesystem::path & path);
void readFile(const std::filesystem::path & path, Sink & sink, bool memory_map = true);

enum struct FsSync { Yes, No };

/**
 * Write a string to a file.
 */
void writeFile(const std::filesystem::path & path, std::string_view s, mode_t mode = 0666, FsSync sync = FsSync::No);

void writeFile(const std::filesystem::path & path, Source & source, mode_t mode = 0666, FsSync sync = FsSync::No);

void writeFile(
    Descriptor fd, std::string_view s, FsSync sync = FsSync::No, const std::filesystem::path * origPath = nullptr);

/**
 * Flush a path's parent directory to disk.
 */
void syncParent(const std::filesystem::path & path);

/**
 * Flush a file or entire directory tree to disk.
 */
void recursiveSync(const std::filesystem::path & path);

/**
 * Delete a path; i.e., in the case of a directory, it is deleted
 * recursively. It's not an error if the path does not exist. The
 * second variant returns the number of bytes and blocks freed.
 */
void deletePath(const std::filesystem::path & path);

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
 * Set the access and modification times of the given path, not
 * following symlinks.
 *
 * @param accessedTime Specified in seconds.
 *
 * @param modificationTime Specified in seconds.
 *
 * @param isSymlink Whether the file in question is a symlink. Used for
 * fallback code where we don't have `lutimes` or similar. if
 * `std::optional` is passed, the information will be recomputed if it
 * is needed. Race conditions are possible so be careful!
 */
void setWriteTime(
    const std::filesystem::path & path,
    time_t accessedTime,
    time_t modificationTime,
    std::optional<bool> isSymlink = std::nullopt);

/**
 * Convenience wrapper that takes all arguments from the `PosixStat`.
 */
void setWriteTime(const std::filesystem::path & path, const PosixStat & st);

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
 * Similar to 'renameFile', but fallback to a copy+remove if `src` and `dst`
 * are on a different filesystem.
 *
 * Beware that this might not be atomic because of the copy that happens behind
 * the scenes
 */
void moveFile(const std::filesystem::path & src, const std::filesystem::path & dst);

/**
 * Recursively copy the content of `oldPath` to `newPath`. If `andDelete` is
 * `true`, then also remove `oldPath` (making this equivalent to `moveFile`, but
 * with the guaranty that the destination will be “fresh”, with no stale inode
 * or file descriptor pointing to it).
 */
void copyFile(const std::filesystem::path & from, const std::filesystem::path & to, bool andDelete);

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

struct DIRDeleter
{
    void operator()(DIR * dir) const
    {
        closedir(dir);
    }
};

typedef std::unique_ptr<DIR, DIRDeleter> AutoCloseDir;

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

/**
 * Remove a file, throwing an exception on error.
 *
 * @param path Path to the file to remove.
 */
void unlink(const std::filesystem::path & path);

/**
 * Try to remove a file, ignoring errors.
 *
 * @param path Path to the file to try to remove.
 */
void tryUnlink(const std::filesystem::path & path);

/**
 * @brief A directory iterator that can be used to iterate over the
 * contents of a directory. It is similar to std::filesystem::directory_iterator
 * but throws NixError on failure instead of std::filesystem::filesystem_error.
 */
class DirectoryIterator
{
public:
    // --- Iterator Traits ---
    using iterator_category = std::input_iterator_tag;
    using value_type = std::filesystem::directory_entry;
    using difference_type = std::ptrdiff_t;
    using pointer = const std::filesystem::directory_entry *;
    using reference = const std::filesystem::directory_entry &;

    // Default constructor (represents end iterator)
    DirectoryIterator() noexcept = default;

    // Constructor taking a path
    explicit DirectoryIterator(const std::filesystem::path & p);

    reference operator*() const
    {
        // Accessing the value itself doesn't typically throw filesystem_error
        // after successful construction/increment, but underlying operations might.
        // If directory_entry methods called via -> could throw, add try-catch there.
        return *it_;
    }

    pointer operator->() const
    {
        return &(*it_);
    }

    DirectoryIterator & operator++();

    // Postfix increment operator
    DirectoryIterator operator++(int)
    {
        DirectoryIterator temp = *this;
        ++(*this); // Uses the prefix increment's try-catch logic
        return temp;
    }

    // Equality comparison
    friend bool operator==(const DirectoryIterator & a, const DirectoryIterator & b) noexcept
    {
        return a.it_ == b.it_;
    }

    // Inequality comparison
    friend bool operator!=(const DirectoryIterator & a, const DirectoryIterator & b) noexcept
    {
        return !(a == b);
    }

    // Allow direct use in range-based for loops if iterating over an instance
    DirectoryIterator begin() const
    {
        return *this;
    }

    DirectoryIterator end() const
    {
        return DirectoryIterator{};
    }


private:
    std::filesystem::directory_iterator it_;
};

#ifdef __FreeBSD__
class AutoUnmount
{
    std::filesystem::path path;
    bool del;
public:
    AutoUnmount();
    AutoUnmount(const std::filesystem::path &);
    AutoUnmount(const AutoUnmount &) = delete;

    AutoUnmount(AutoUnmount && other) noexcept
        : path(std::move(other.path))
        , del(std::exchange(other.del, false))
    {
    }

    AutoUnmount & operator=(AutoUnmount && other) noexcept
    {
        swap(*this, other);
        return *this;
    }

    friend void swap(AutoUnmount & lhs, AutoUnmount & rhs) noexcept
    {
        using std::swap;
        swap(lhs.path, rhs.path);
        swap(lhs.del, rhs.del);
    }

    ~AutoUnmount();

    /**
     * Cancel the unmounting
     */
    void cancel() noexcept;

    /**
     * Unmount the mountpoint right away (if it exists), resetting the
     * `AutoUnmount`
     *
     * The destructor calls this, but ignoring any exception.
     */
    void unmount();
};
#endif

} // namespace nix
