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

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>

#include <boost/lexical_cast.hpp>

#include <atomic>
#include <functional>
#include <map>
#include <sstream>
#include <optional>

#ifndef HAVE_STRUCT_DIRENT_D_TYPE
#define DT_UNKNOWN 0
#define DT_REG 1
#define DT_LNK 2
#define DT_DIR 3
#endif

namespace nix {

struct Sink;
struct Source;

/**
 * @return An absolutized path, resolving paths relative to the
 * specified directory, or the current directory otherwise.  The path
 * is also canonicalised.
 */
Path absPath(PathView path,
    std::optional<PathView> dir = {},
    bool resolveSymlinks = false);

/**
 * Canonicalise a path by removing all `.` or `..` components and
 * double or trailing slashes.  Optionally resolves all symlink
 * components such that each component of the resulting path is *not*
 * a symbolic link.
 */
Path canonPath(PathView path, bool resolveSymlinks = false);

/**
 * @return The directory part of the given canonical path, i.e.,
 * everything before the final `/`.  If the path is the root or an
 * immediate child thereof (e.g., `/foo`), this means `/`
 * is returned.
 */
Path dirOf(const PathView path);

/**
 * @return the base name of the given canonical path, i.e., everything
 * following the final `/` (trailing slashes are removed).
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
 * @return true iff the given path exists.
 */
bool pathExists(const Path & path);

/**
 * A version of pathExists that returns false on a permission error.
 * Useful for inferring default paths across directories that might not
 * be readable.
 * @return true iff the given path can be accessed and exists
 */
bool pathAccessible(const Path & path);

/**
 * Read the contents (target) of a symbolic link.  The result is not
 * in any way canonicalised.
 */
Path readLink(const Path & path);

bool isLink(const Path & path);

/**
 * Read the contents of a directory.  The entries `.` and `..` are
 * removed.
 */
struct DirEntry
{
    std::string name;
    ino_t ino;
    /**
     * one of DT_*
     */
    unsigned char type;
    DirEntry(std::string name, ino_t ino, unsigned char type)
        : name(std::move(name)), ino(ino), type(type) { }
};

typedef std::vector<DirEntry> DirEntries;

DirEntries readDirectory(const Path & path);

unsigned char getFileType(const Path & path);

/**
 * Read the contents of a file into a string.
 */
std::string readFile(const Path & path);
void readFile(const Path & path, Sink & sink);

/**
 * Write a string to a file.
 */
void writeFile(const Path & path, std::string_view s, mode_t mode = 0666, bool sync = false);

void writeFile(const Path & path, Source & source, mode_t mode = 0666, bool sync = false);

/**
 * Flush a file's parent directory to disk
 */
void syncParent(const Path & path);

/**
 * Delete a path; i.e., in the case of a directory, it is deleted
 * recursively. It's not an error if the path does not exist. The
 * second variant returns the number of bytes and blocks freed.
 */
void deletePath(const Path & path);

void deletePath(const Path & path, uint64_t & bytesFreed);

/**
 * Create a directory and all its parents, if necessary.  Returns the
 * list of created directories, in order of creation.
 */
Paths createDirs(const Path & path);
inline Paths createDirs(PathView path)
{
    return createDirs(Path(path));
}

/**
 * Create a symlink.
 */
void createSymlink(const Path & target, const Path & link);

/**
 * Atomically create or replace a symlink.
 */
void replaceSymlink(const Path & target, const Path & link);

void renameFile(const Path & src, const Path & dst);

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
void copyFile(const Path & oldPath, const Path & newPath, bool andDelete);

/**
 * Automatic cleanup of resources.
 */
class AutoDelete
{
    Path path;
    bool del;
    bool recursive;
public:
    AutoDelete();
    AutoDelete(const Path & p, bool recursive = true);
    ~AutoDelete();
    void cancel();
    void reset(const Path & p, bool recursive = true);
    operator Path() const { return path; }
    operator PathView() const { return path; }
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
 * Used in various places.
 */
typedef std::function<bool(const Path & path)> PathFilter;

extern PathFilter defaultPathFilter;

}
