#pragma once
/**
 * @file
 *
 * Utiltities for working with the file sytem and file paths.
 */

#include "types.hh"
#include "error.hh"
#include "logging.hh"
#include "file-descriptor.hh"
#include "file-path.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#ifdef _WIN32
# include <windef.h>
#endif
#include <signal.h>

#include <atomic>
#include <functional>
#include <map>
#include <sstream>
#include <optional>

/**
 * Polyfill for MinGW
 *
 * Windows does in fact support symlinks, but the C runtime interfaces predate this.
 *
 * @todo get rid of this, and stop using `stat` when we want `lstat` too.
 */
#ifndef S_ISLNK
# define S_ISLNK(m) false
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
Path absPath(PathView path,
    std::optional<PathView> dir = {},
    bool resolveSymlinks = false);

inline Path absPath(const Path & path,
    std::optional<PathView> dir = {},
    bool resolveSymlinks = false)
{
    return absPath(PathView{path}, dir, resolveSymlinks);
}

std::filesystem::path absPath(const std::filesystem::path & path,
    bool resolveSymlinks = false);

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
Path canonPath(PathView path, bool resolveSymlinks = false);

/**
 * @return The directory part of the given canonical path, i.e.,
 * everything before the final `/`.  If the path is the root or an
 * immediate child thereof (e.g., `/foo`), this means `/`
 * is returned.
 *
 * In the process of being deprecated for
 * `std::filesystem::path::parent_path`.
 */
Path dirOf(const PathView path);

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
bool isInDir(std::string_view path, std::string_view dir);

/**
 * Check whether 'path' is equal to 'dir' or a descendant of
 * 'dir'. Both paths must be canonicalized.
 */
bool isDirOrInDir(std::string_view path, std::string_view dir);

/**
 * Get status of `path`.
 */
struct stat stat(const Path & path);
struct stat lstat(const Path & path);
/**
 * `lstat` the given path if it exists.
 * @return std::nullopt if the path doesn't exist, or an optional containing the result of `lstat` otherwise
 */
std::optional<struct stat> maybeLstat(const Path & path);

/**
 * @return true iff the given path exists.
 *
 * In the process of being deprecated for `fs::symlink_exists`.
 */
bool pathExists(const Path & path);

namespace fs {

/**
 *  ```
 *  symlink_exists(p) = std::filesystem::exists(std::filesystem::symlink_status(p))
 *  ```
 *  Missing convenience analogous to
 *  ```
 *  std::filesystem::exists(p) = std::filesystem::exists(std::filesystem::status(p))
 *  ```
 */
inline bool symlink_exists(const std::filesystem::path & path) {
    return std::filesystem::exists(std::filesystem::symlink_status(path));
}

} // namespace fs

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
 *
 * In the process of being deprecated for
 * `std::filesystem::read_symlink`.
 */
Path readLink(const Path & path);

/**
 * Open a `Descriptor` with read-only access to the given directory.
 */
Descriptor openDirectory(const std::filesystem::path & path);

/**
 * Read the contents of a file into a string.
 */
std::string readFile(const Path & path);
std::string readFile(const std::filesystem::path & path);
void readFile(const Path & path, Sink & sink);

/**
 * Write a string to a file.
 */
void writeFile(const Path & path, std::string_view s, mode_t mode = 0666, bool sync = false);
static inline void writeFile(const std::filesystem::path & path, std::string_view s, mode_t mode = 0666, bool sync = false)
{
    return writeFile(path.string(), s, mode, sync);
}

void writeFile(const Path & path, Source & source, mode_t mode = 0666, bool sync = false);
static inline void writeFile(const std::filesystem::path & path, Source & source, mode_t mode = 0666, bool sync = false)
{
    return writeFile(path.string(), source, mode, sync);
}

/**
 * Flush a path's parent directory to disk.
 */
void syncParent(const Path & path);

/**
 * Flush a file or entire directory tree to disk.
 */
void recursiveSync(const Path & path);

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
 * In the process of being deprecated for
 * `std::filesystem::create_directories`.
 */
void createDirs(const Path & path);
inline void createDirs(PathView path)
{
    return createDirs(Path(path));
}

/**
 * Create a single directory.
 */
void createDir(const Path & path, mode_t mode = 0755);

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
 * Convenience wrapper that takes all arguments from the `struct stat`.
 */
void setWriteTime(const std::filesystem::path & path, const struct stat & st);

/**
 * Create a symlink.
 *
 */
void createSymlink(const Path & target, const Path & link);

/**
 * Atomically create or replace a symlink.
 */
void replaceSymlink(const std::filesystem::path & target, const std::filesystem::path & link);

inline void replaceSymlink(const Path & target, const Path & link)
{
    return replaceSymlink(std::filesystem::path{target}, std::filesystem::path{link});
}

/**
 * Similar to 'renameFile', but fallback to a copy+remove if `src` and `dst`
 * are on a different filesystem.
 *
 * Beware that this might not be atomic because of the copy that happens behind
 * the scenes
 */
void moveFile(const Path & src, const Path & dst);

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
    AutoDelete(const std::filesystem::path & p, bool recursive = true);
    ~AutoDelete();

    void cancel();

    void reset(const std::filesystem::path & p, bool recursive = true);

    const std::filesystem::path & path() const { return _path; }
    PathViewNG view() const { return _path; }

    operator const std::filesystem::path & () const { return _path; }
    operator PathViewNG () const { return _path; }
};


struct DIRDeleter
{
    void operator()(DIR * dir) const {
        closedir(dir);
    }
};

typedef std::unique_ptr<DIR, DIRDeleter> AutoCloseDir;


/**
 * Create a temporary directory.
 */
Path createTempDir(const Path & tmpRoot = "", const Path & prefix = "nix",
    bool includePid = true, bool useGlobalCounter = true, mode_t mode = 0755);

/**
 * Create a temporary file, returning a file handle and its path.
 */
std::pair<AutoCloseFD, Path> createTempFile(const Path & prefix = "nix");

/**
 * Return `TMPDIR`, or the default temporary directory if unset or empty.
 */
Path defaultTempDir();

/**
 * Interpret `exe` as a location in the ambient file system and return
 * whether it resolves to a file that is executable.
 */
bool isExecutableFileAmbient(const std::filesystem::path & exe);

/**
 * Used in various places.
 */
typedef std::function<bool(const Path & path)> PathFilter;

extern PathFilter defaultPathFilter;

}
