#include "environment-variables.hh"
#include "file-system.hh"
#include "file-path.hh"
#include "file-path-impl.hh"
#include "signals.hh"
#include "finally.hh"
#include "serialise.hh"

#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <filesystem>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef _WIN32
# include <io.h>
#endif

namespace fs = std::filesystem;

namespace nix {

/**
 * Treat the string as possibly an absolute path, by inspecting the
 * start of it. Return whether it was probably intended to be
 * absolute.
 */
static bool isAbsolute(PathView path)
{
    return fs::path { path }.is_absolute();
}


Path absPath(PathView path, std::optional<PathView> dir, bool resolveSymlinks)
{
    std::string scratch;

    if (!isAbsolute(path)) {
        // In this case we need to call `canonPath` on a newly-created
        // string. We set `scratch` to that string first, and then set
        // `path` to `scratch`. This ensures the newly-created string
        // lives long enough for the call to `canonPath`, and allows us
        // to just accept a `std::string_view`.
        if (!dir) {
#ifdef __GNU__
            /* GNU (aka. GNU/Hurd) doesn't have any limitation on path
               lengths and doesn't define `PATH_MAX'.  */
            char *buf = getcwd(NULL, 0);
            if (buf == NULL)
#else
            char buf[PATH_MAX];
            if (!getcwd(buf, sizeof(buf)))
#endif
                throw SysError("cannot get cwd");
            scratch = concatStrings(buf, "/", path);
#ifdef __GNU__
            free(buf);
#endif
        } else
            scratch = concatStrings(*dir, "/", path);
        path = scratch;
    }
    return canonPath(path, resolveSymlinks);
}


Path canonPath(PathView path, bool resolveSymlinks)
{
    assert(path != "");

    if (!isAbsolute(path))
        throw Error("not an absolute path: '%1%'", path);

    // For Windows
    auto rootName = fs::path { path }.root_name();

    /* This just exists because we cannot set the target of `remaining`
       (the callback parameter) directly to a newly-constructed string,
       since it is `std::string_view`. */
    std::string temp;

    /* Count the number of times we follow a symlink and stop at some
       arbitrary (but high) limit to prevent infinite loops. */
    unsigned int followCount = 0, maxFollow = 1024;

    auto ret = canonPathInner<NativePathTrait>(
        path,
        [&followCount, &temp, maxFollow, resolveSymlinks]
        (std::string & result, std::string_view & remaining) {
            if (resolveSymlinks && fs::is_symlink(result)) {
                if (++followCount >= maxFollow)
                    throw Error("infinite symlink recursion in path '%0%'", remaining);
                remaining = (temp = concatStrings(readLink(result), remaining));
                if (isAbsolute(remaining)) {
                    /* restart for symlinks pointing to absolute path */
                    result.clear();
                } else {
                    result = dirOf(result);
                    if (result == "/") {
                        /* we don’t want trailing slashes here, which `dirOf`
                           only produces if `result = /` */
                        result.clear();
                    }
                }
            }
        });

    if (!rootName.empty())
        ret = rootName.string() + std::move(ret);
    return ret;
}


Path dirOf(const PathView path)
{
    Path::size_type pos = NativePathTrait::rfindPathSep(path);
    if (pos == path.npos)
        return ".";
    return fs::path{path}.parent_path().string();
}


std::string_view baseNameOf(std::string_view path)
{
    if (path.empty())
        return "";

    auto last = path.size() - 1;
    while (last > 0 && NativePathTrait::isPathSep(path[last]))
        last -= 1;

    auto pos = NativePathTrait::rfindPathSep(path, last);
    if (pos == path.npos)
        pos = 0;
    else
        pos += 1;

    return path.substr(pos, last - pos + 1);
}


bool isInDir(std::string_view path, std::string_view dir)
{
    return path.substr(0, 1) == "/"
        && path.substr(0, dir.size()) == dir
        && path.size() >= dir.size() + 2
        && path[dir.size()] == '/';
}


bool isDirOrInDir(std::string_view path, std::string_view dir)
{
    return path == dir || isInDir(path, dir);
}


struct stat stat(const Path & path)
{
    struct stat st;
    if (stat(path.c_str(), &st))
        throw SysError("getting status of '%1%'", path);
    return st;
}

#ifdef _WIN32
# define STAT stat
#else
# define STAT lstat
#endif

struct stat lstat(const Path & path)
{
    struct stat st;
    if (STAT(path.c_str(), &st))
        throw SysError("getting status of '%1%'", path);
    return st;
}


std::optional<struct stat> maybeLstat(const Path & path)
{
    std::optional<struct stat> st{std::in_place};
    if (STAT(path.c_str(), &*st))
    {
        if (errno == ENOENT || errno == ENOTDIR)
            st.reset();
        else
            throw SysError("getting status of '%s'", path);
    }
    return st;
}


bool pathExists(const Path & path)
{
    return maybeLstat(path).has_value();
}

bool pathAccessible(const Path & path)
{
    try {
        return pathExists(path);
    } catch (SysError & e) {
        // swallow EPERM
        if (e.errNo == EPERM) return false;
        throw;
    }
}


Path readLink(const Path & path)
{
    checkInterrupt();
    return fs::read_symlink(path).string();
}


std::string readFile(const Path & path)
{
    AutoCloseFD fd = toDescriptor(open(path.c_str(), O_RDONLY
// TODO
#ifndef _WIN32
       | O_CLOEXEC
#endif
       ));
    if (!fd)
        throw SysError("opening file '%1%'", path);
    return readFile(fd.get());
}


void readFile(const Path & path, Sink & sink)
{
    AutoCloseFD fd = toDescriptor(open(path.c_str(), O_RDONLY
// TODO
#ifndef _WIN32
       | O_CLOEXEC
#endif
       ));
    if (!fd)
        throw SysError("opening file '%s'", path);
    drainFD(fd.get(), sink);
}


void writeFile(const Path & path, std::string_view s, mode_t mode, bool sync)
{
    AutoCloseFD fd = toDescriptor(open(path.c_str(), O_WRONLY | O_TRUNC | O_CREAT
// TODO
#ifndef _WIN32
       | O_CLOEXEC
#endif
       , mode));
    if (!fd)
        throw SysError("opening file '%1%'", path);
    try {
        writeFull(fd.get(), s);
    } catch (Error & e) {
        e.addTrace({}, "writing file '%1%'", path);
        throw;
    }
    if (sync)
        fd.fsync();
    // Explicitly close to make sure exceptions are propagated.
    fd.close();
    if (sync)
        syncParent(path);
}


void writeFile(const Path & path, Source & source, mode_t mode, bool sync)
{
    AutoCloseFD fd = toDescriptor(open(path.c_str(), O_WRONLY | O_TRUNC | O_CREAT
// TODO
#ifndef _WIN32
       | O_CLOEXEC
#endif
       , mode));
    if (!fd)
        throw SysError("opening file '%1%'", path);

    std::array<char, 64 * 1024> buf;

    try {
        while (true) {
            try {
                auto n = source.read(buf.data(), buf.size());
                writeFull(fd.get(), {buf.data(), n});
            } catch (EndOfFile &) { break; }
        }
    } catch (Error & e) {
        e.addTrace({}, "writing file '%1%'", path);
        throw;
    }
    if (sync)
        fd.fsync();
    // Explicitly close to make sure exceptions are propagated.
    fd.close();
    if (sync)
        syncParent(path);
}

void syncParent(const Path & path)
{
    AutoCloseFD fd = toDescriptor(open(dirOf(path).c_str(), O_RDONLY, 0));
    if (!fd)
        throw SysError("opening file '%1%'", path);
    fd.fsync();
}


static void _deletePath(Descriptor parentfd, const fs::path & path, uint64_t & bytesFreed)
{
#ifndef _WIN32
    checkInterrupt();

    std::string name(baseNameOf(path.native()));

    struct stat st;
    if (fstatat(parentfd, name.c_str(), &st,
            AT_SYMLINK_NOFOLLOW) == -1) {
        if (errno == ENOENT) return;
        throw SysError("getting status of '%1%'", path);
    }

    if (!S_ISDIR(st.st_mode)) {
        /* We are about to delete a file. Will it likely free space? */

        switch (st.st_nlink) {
            /* Yes: last link. */
            case 1:
                bytesFreed += st.st_size;
                break;
            /* Maybe: yes, if 'auto-optimise-store' or manual optimisation
               was performed. Instead of checking for real let's assume
               it's an optimised file and space will be freed.

               In worst case we will double count on freed space for files
               with exactly two hardlinks for unoptimised packages.
             */
            case 2:
                bytesFreed += st.st_size;
                break;
            /* No: 3+ links. */
            default:
                break;
        }
    }

    if (S_ISDIR(st.st_mode)) {
        /* Make the directory accessible. */
        const auto PERM_MASK = S_IRUSR | S_IWUSR | S_IXUSR;
        if ((st.st_mode & PERM_MASK) != PERM_MASK) {
            if (fchmodat(parentfd, name.c_str(), st.st_mode | PERM_MASK, 0) == -1)
                throw SysError("chmod '%1%'", path);
        }

        int fd = openat(parentfd, path.c_str(), O_RDONLY);
        if (fd == -1)
            throw SysError("opening directory '%1%'", path);
        AutoCloseDir dir(fdopendir(fd));
        if (!dir)
            throw SysError("opening directory '%1%'", path);

        struct dirent * dirent;
        while (errno = 0, dirent = readdir(dir.get())) { /* sic */
            checkInterrupt();
            std::string childName = dirent->d_name;
            if (childName == "." || childName == "..") continue;
            _deletePath(dirfd(dir.get()), path + "/" + childName, bytesFreed);
        }
        if (errno) throw SysError("reading directory '%1%'", path);
    }

    int flags = S_ISDIR(st.st_mode) ? AT_REMOVEDIR : 0;
    if (unlinkat(parentfd, name.c_str(), flags) == -1) {
        if (errno == ENOENT) return;
        throw SysError("cannot unlink '%1%'", path);
    }
#else
    // TODO implement
    throw UnimplementedError("_deletePath");
#endif
}

static void _deletePath(const fs::path & path, uint64_t & bytesFreed)
{
    Path dir = dirOf(path.string());
    if (dir == "")
        dir = "/";

    AutoCloseFD dirfd = toDescriptor(open(dir.c_str(), O_RDONLY));
    if (!dirfd) {
        if (errno == ENOENT) return;
        throw SysError("opening directory '%1%'", path);
    }

    _deletePath(dirfd.get(), path, bytesFreed);
}


void deletePath(const fs::path & path)
{
    uint64_t dummy;
    deletePath(path, dummy);
}


Paths createDirs(const Path & path)
{
    Paths created;
    if (path == "/") return created;

    struct stat st;
    if (STAT(path.c_str(), &st) == -1) {
        created = createDirs(dirOf(path));
        if (mkdir(path.c_str()
#ifndef _WIN32 // TODO abstract mkdir perms for Windows
            , 0777
#endif
            ) == -1 && errno != EEXIST)
            throw SysError("creating directory '%1%'", path);
        st = STAT(path);
        created.push_back(path);
    }

    if (S_ISLNK(st.st_mode) && stat(path.c_str(), &st) == -1)
        throw SysError("statting symlink '%1%'", path);

    if (!S_ISDIR(st.st_mode)) throw Error("'%1%' is not a directory", path);

    return created;
}


void deletePath(const fs::path & path, uint64_t & bytesFreed)
{
    //Activity act(*logger, lvlDebug, "recursively deleting path '%1%'", path);
    bytesFreed = 0;
    _deletePath(path, bytesFreed);
}


//////////////////////////////////////////////////////////////////////

AutoDelete::AutoDelete() : del{false} {}

AutoDelete::AutoDelete(const fs::path & p, bool recursive) : _path(p)
{
    del = true;
    this->recursive = recursive;
}

AutoDelete::~AutoDelete()
{
    try {
        if (del) {
            if (recursive)
                deletePath(_path);
            else {
                fs::remove(_path);
            }
        }
    } catch (...) {
        ignoreException();
    }
}

void AutoDelete::cancel()
{
    del = false;
}

void AutoDelete::reset(const fs::path & p, bool recursive) {
    _path = p;
    this->recursive = recursive;
    del = true;
}

//////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////

std::string defaultTempDir() {
    return getEnvNonEmpty("TMPDIR").value_or("/tmp");
}

static Path tempName(Path tmpRoot, const Path & prefix, bool includePid,
    std::atomic<unsigned int> & counter)
{
    tmpRoot = canonPath(tmpRoot.empty() ? defaultTempDir() : tmpRoot, true);
    if (includePid)
        return fmt("%1%/%2%-%3%-%4%", tmpRoot, prefix, getpid(), counter++);
    else
        return fmt("%1%/%2%-%3%", tmpRoot, prefix, counter++);
}

Path createTempDir(const Path & tmpRoot, const Path & prefix,
    bool includePid, bool useGlobalCounter, mode_t mode)
{
    static std::atomic<unsigned int> globalCounter = 0;
    std::atomic<unsigned int> localCounter = 0;
    auto & counter(useGlobalCounter ? globalCounter : localCounter);

    while (1) {
        checkInterrupt();
        Path tmpDir = tempName(tmpRoot, prefix, includePid, counter);
        if (mkdir(tmpDir.c_str()
#ifndef _WIN32 // TODO abstract mkdir perms for Windows
                    , mode
#endif
                    ) == 0) {
#if __FreeBSD__
            /* Explicitly set the group of the directory.  This is to
               work around around problems caused by BSD's group
               ownership semantics (directories inherit the group of
               the parent).  For instance, the group of /tmp on
               FreeBSD is "wheel", so all directories created in /tmp
               will be owned by "wheel"; but if the user is not in
               "wheel", then "tar" will fail to unpack archives that
               have the setgid bit set on directories. */
            if (chown(tmpDir.c_str(), (uid_t) -1, getegid()) != 0)
                throw SysError("setting group of directory '%1%'", tmpDir);
#endif
            return tmpDir;
        }
        if (errno != EEXIST)
            throw SysError("creating directory '%1%'", tmpDir);
    }
}


std::pair<AutoCloseFD, Path> createTempFile(const Path & prefix)
{
    Path tmpl(defaultTempDir() + "/" + prefix + ".XXXXXX");
    // Strictly speaking, this is UB, but who cares...
    // FIXME: use O_TMPFILE.
    AutoCloseFD fd = toDescriptor(mkstemp((char *) tmpl.c_str()));
    if (!fd)
        throw SysError("creating temporary file '%s'", tmpl);
#ifndef _WIN32
    closeOnExec(fd.get());
#endif
    return {std::move(fd), tmpl};
}

void createSymlink(const Path & target, const Path & link)
{
    fs::create_symlink(target, link);
}

void replaceSymlink(const Path & target, const Path & link)
{
    for (unsigned int n = 0; true; n++) {
        Path tmp = canonPath(fmt("%s/.%d_%s", dirOf(link), n, baseNameOf(link)));

        try {
            createSymlink(target, tmp);
        } catch (fs::filesystem_error & e) {
            if (e.code() == std::errc::file_exists) continue;
            throw;
        }

        std::filesystem::rename(tmp, link);

        break;
    }
}

#ifndef _WIN32
static void setWriteTime(const fs::path & p, const struct stat & st)
{
    struct timeval times[2];
    times[0] = {
        .tv_sec = st.st_atime,
        .tv_usec = 0,
    };
    times[1] = {
        .tv_sec = st.st_mtime,
        .tv_usec = 0,
    };
    if (lutimes(p.c_str(), times) != 0)
        throw SysError("changing modification time of '%s'", p);
}
#endif

void copyFile(const fs::path & from, const fs::path & to, bool andDelete)
{
#ifndef _WIN32
    // TODO: Rewrite the `is_*` to use `symlink_status()`
    auto statOfFrom = lstat(from.c_str());
#endif
    auto fromStatus = fs::symlink_status(from);

    // Mark the directory as writable so that we can delete its children
    if (andDelete && fs::is_directory(fromStatus)) {
        fs::permissions(from, fs::perms::owner_write, fs::perm_options::add | fs::perm_options::nofollow);
    }


    if (fs::is_symlink(fromStatus) || fs::is_regular_file(fromStatus)) {
        fs::copy(from, to, fs::copy_options::copy_symlinks | fs::copy_options::overwrite_existing);
    } else if (fs::is_directory(fromStatus)) {
        fs::create_directory(to);
        for (auto & entry : fs::directory_iterator(from)) {
            copyFile(entry, to / entry.path().filename(), andDelete);
        }
    } else {
        throw Error("file '%s' has an unsupported type", from);
    }

#ifndef _WIN32
    setWriteTime(to, statOfFrom);
#endif
    if (andDelete) {
        if (!fs::is_symlink(fromStatus))
            fs::permissions(from, fs::perms::owner_write, fs::perm_options::add | fs::perm_options::nofollow);
        fs::remove(from);
    }
}

void moveFile(const Path & oldName, const Path & newName)
{
    try {
        std::filesystem::rename(oldName, newName);
    } catch (fs::filesystem_error & e) {
        auto oldPath = fs::path(oldName);
        auto newPath = fs::path(newName);
        // For the move to be as atomic as possible, copy to a temporary
        // directory
        fs::path temp = createTempDir(
            os_string_to_string(PathViewNG { newPath.parent_path() }),
            "rename-tmp");
        Finally removeTemp = [&]() { fs::remove(temp); };
        auto tempCopyTarget = temp / "copy-target";
        if (e.code().value() == EXDEV) {
            fs::remove(newPath);
            warn("Can’t rename %s as %s, copying instead", oldName, newName);
            copyFile(oldPath, tempCopyTarget, true);
            std::filesystem::rename(
                os_string_to_string(PathViewNG { tempCopyTarget }),
                os_string_to_string(PathViewNG { newPath }));
        }
    }
}

//////////////////////////////////////////////////////////////////////

}
