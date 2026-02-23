#include "nix/util/environment-variables.hh"
#include "nix/util/file-system.hh"
#include "nix/util/file-path.hh"
#include "nix/util/file-path-impl.hh"
#include "nix/util/signals.hh"
#include "nix/util/finally.hh"
#include "nix/util/serialise.hh"
#include "nix/util/util.hh"

#include <atomic>
#include <random>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/filesystem/path.hpp>

#ifdef __FreeBSD__
#  include <sys/param.h>
#  include <sys/mount.h>
#endif

#ifdef _WIN32
#  include <io.h>
#endif

namespace nix {

DirectoryIterator::DirectoryIterator(const std::filesystem::path & p)
{
    try {
        // **Attempt to create the underlying directory_iterator**
        it_ = std::filesystem::directory_iterator(p);
    } catch (const std::filesystem::filesystem_error & e) {
        // **Catch filesystem_error and throw SystemError**
        // Adapt the error message as needed for SystemError
        throw SystemError(e.code(), "cannot read directory %s", PathFmt(p));
    }
}

DirectoryIterator & DirectoryIterator::operator++()
{
    // **Attempt to increment the underlying iterator**
    std::error_code ec;
    it_.increment(ec);
    if (ec) {
        // Try to get path info if possible, might fail if iterator is bad
        try {
            if (it_ != std::filesystem::directory_iterator{}) {
                throw SysError("cannot read directory past %s: %s", PathFmt(it_->path()), ec.message());
            }
        } catch (...) {
            throw SysError("cannot read directory");
        }
    }
    return *this;
}

bool isAbsolute(PathView path)
{
    return std::filesystem::path{path}.is_absolute();
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
            char * buf = getcwd(NULL, 0);
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

std::filesystem::path
absPath(const std::filesystem::path & path, const std::filesystem::path * dir_, bool resolveSymlinks)
{
    std::optional<std::string> dir = dir_ ? std::optional<std::string>{dir_->string()} : std::nullopt;
    return absPath(PathView{path.string()}, dir.transform([](auto & p) { return PathView(p); }), resolveSymlinks);
}

Path canonPath(PathView path, bool resolveSymlinks)
{
    assert(path != "");

    if (!isAbsolute(path))
        throw Error("not an absolute path: '%1%'", path);

    /* This just exists because we cannot set the target of `remaining`
       (the callback parameter) directly to a newly-constructed string,
       since it is `std::string_view`. */
    std::string temp;

    /* Count the number of times we follow a symlink and stop at some
       arbitrary (but high) limit to prevent infinite loops. */
    unsigned int followCount = 0, maxFollow = 1024;

    auto ret = canonPathInner<OsPathTrait<char>>(
        path, [&followCount, &temp, maxFollow, resolveSymlinks](std::string & result, std::string_view & remaining) {
            if (resolveSymlinks && std::filesystem::is_symlink(result)) {
                if (++followCount >= maxFollow)
                    throw Error("infinite symlink recursion in path '%1%'", remaining);
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

    return ret;
}

std::filesystem::path canonPath(const std::filesystem::path & path, bool resolveSymlinks)
{
    return {canonPath(path.string(), resolveSymlinks)};
}

Path dirOf(const PathView path)
{
    Path::size_type pos = OsPathTrait<char>::rfindPathSep(path);
    if (pos == path.npos)
        return ".";
    return std::filesystem::path{path}.parent_path().string();
}

std::string_view baseNameOf(std::string_view path)
{
    if (path.empty())
        return "";

    auto last = path.size() - 1;
    while (last > 0 && OsPathTrait<char>::isPathSep(path[last]))
        last -= 1;

    auto pos = OsPathTrait<char>::rfindPathSep(path, last);
    if (pos == path.npos)
        pos = 0;
    else
        pos += 1;

    return path.substr(pos, last - pos + 1);
}

bool isInDir(const std::filesystem::path & path, const std::filesystem::path & dir)
{
    /* Note that while the standard doesn't guarantee this, the
      `lexically_*` functions should do no IO and not throw. */
    auto rel = path.lexically_relative(dir);
    if (rel.empty())
        return false;

    auto first = *rel.begin();
    return first != "." && first != "..";
}

bool isDirOrInDir(const std::filesystem::path & path, const std::filesystem::path & dir)
{
    return path == dir || isInDir(path, dir);
}

#ifdef _WIN32
#  define STAT _wstat64
#  define LSTAT _wstat64
#else
#  define STAT stat
#  define LSTAT lstat
#endif

PosixStat stat(const std::filesystem::path & path)
{
    PosixStat st;
    if (STAT(path.c_str(), &st))
        throw SysError("getting status of %s", PathFmt(path));
    return st;
}

PosixStat lstat(const std::filesystem::path & path)
{
    PosixStat st;
    if (LSTAT(path.c_str(), &st))
        throw SysError("getting status of %s", PathFmt(path));
    return st;
}

PosixStat fstat(int fd)
{
    PosixStat st;
    if (
#ifdef _WIN32
        _fstat64
#else
        ::fstat
#endif
        (fd, &st))
        throw SysError("getting status of fd %d", fd);
    return st;
}

std::optional<PosixStat> maybeStat(const std::filesystem::path & path)
{
    std::optional<PosixStat> st{std::in_place};
    if (STAT(path.c_str(), &*st)) {
        if (errno == ENOENT || errno == ENOTDIR)
            st.reset();
        else
            throw SysError("getting status of %s", PathFmt(path));
    }
    return st;
}

std::optional<PosixStat> maybeLstat(const std::filesystem::path & path)
{
    std::optional<PosixStat> st{std::in_place};
    if (LSTAT(path.c_str(), &*st)) {
        if (errno == ENOENT || errno == ENOTDIR)
            st.reset();
        else
            throw SysError("getting status of %s", PathFmt(path));
    }
    return st;
}

#undef STAT
#undef LSTAT

bool pathExists(const std::filesystem::path & path)
{
    return maybeLstat(path).has_value();
}

bool pathAccessible(const std::filesystem::path & path)
{
    try {
        return pathExists(path);
    } catch (SystemError & e) {
        // swallow EPERM
        if (e.is(std::errc::operation_not_permitted))
            return false;
        throw;
    }
}

std::filesystem::path readLink(const std::filesystem::path & path)
{
    checkInterrupt();
    try {
        return std::filesystem::read_symlink(path);
    } catch (std::filesystem::filesystem_error & e) {
        throw SystemError(e.code(), "reading symbolic link '%s'", PathFmt(path));
    }
}

Path readLink(const Path & path)
{
    return readLink(std::filesystem::path{path}).string();
}

std::string readFile(const Path & path)
{
    return readFile(std::filesystem::path(path));
}

std::string readFile(const std::filesystem::path & path)
{
    AutoCloseFD fd = openFileReadonly(path);
    if (!fd)
        throw NativeSysError("opening file %1%", PathFmt(path));
    return readFile(fd.get());
}

void readFile(const std::filesystem::path & path, Sink & sink, bool memory_map)
{
    // Memory-map the file for faster processing where possible.
    if (memory_map) {
        try {
            // mapped_file_source can't be constructed from std::filesystem::path with wide paths. Go
            // through boost::filesystem::path.
            boost::iostreams::mapped_file_source mmap(boost::filesystem::path{path.native()});
            if (mmap.is_open()) {
                sink({mmap.data(), mmap.size()});
                return;
            }
        } catch (const boost::exception & e) {
            debug("memory-mapping failed for path: %s: %s", PathFmt(path), boost::diagnostic_information(e));
        }
    }

    // Stream the file instead if memory-mapping fails or is disabled.
    AutoCloseFD fd = openFileReadonly(path);
    if (!fd)
        throw NativeSysError("opening file %s", PathFmt(path));
    drainFD(fd.get(), sink);
}

void writeFile(const Path & path, std::string_view s, mode_t mode, FsSync sync)
{
    AutoCloseFD fd = toDescriptor(open(
        path.c_str(),
        O_WRONLY | O_TRUNC | O_CREAT
#ifdef O_CLOEXEC
            | O_CLOEXEC
#endif
        ,
        mode));
    if (!fd)
        throw SysError("opening file '%1%'", path);

    writeFile(fd.get(), s, sync, &path);

    /* Close explicitly to propagate the exceptions. */
    fd.close();
}

void writeFile(Descriptor fd, std::string_view s, FsSync sync, const Path * origPath)
{
    assert(fd != INVALID_DESCRIPTOR);
    try {
        writeFull(fd, s);

        if (sync == FsSync::Yes)
            syncDescriptor(fd);

    } catch (Error & e) {
        e.addTrace({}, "writing file '%1%'", origPath ? *origPath : descriptorToPath(fd).string());
        throw;
    }
}

void writeFile(const Path & path, Source & source, mode_t mode, FsSync sync)
{
    AutoCloseFD fd = toDescriptor(open(
        path.c_str(),
        O_WRONLY | O_TRUNC | O_CREAT
#ifdef O_CLOEXEC
            | O_CLOEXEC
#endif
        ,
        mode));
    if (!fd)
        throw SysError("opening file '%1%'", path);

    std::array<char, 64 * 1024> buf;

    try {
        while (true) {
            try {
                auto n = source.read(buf.data(), buf.size());
                writeFull(fd.get(), {buf.data(), n});
            } catch (EndOfFile &) {
                break;
            }
        }
    } catch (Error & e) {
        e.addTrace({}, "writing file '%1%'", path);
        throw;
    }
    if (sync == FsSync::Yes)
        fd.fsync();
    // Explicitly close to make sure exceptions are propagated.
    fd.close();
    if (sync == FsSync::Yes)
        syncParent(path);
}

void syncParent(const Path & path)
{
    AutoCloseFD fd = toDescriptor(open(dirOf(path).c_str(), O_RDONLY, 0));
    if (!fd)
        throw SysError("opening file '%1%'", path);
    fd.fsync();
}

void recursiveSync(const Path & path)
{
    /* If it's a file or symlink, just fsync and return. */
    auto st = lstat(path);
    if (S_ISREG(st.st_mode)) {
        AutoCloseFD fd = toDescriptor(open(path.c_str(), O_RDONLY, 0));
        if (!fd)
            throw SysError("opening file '%1%'", path);
        fd.fsync();
        return;
    } else if (S_ISLNK(st.st_mode))
        return;

    /* Otherwise, perform a depth-first traversal of the directory and
       fsync all the files. */
    std::deque<std::filesystem::path> dirsToEnumerate;
    dirsToEnumerate.push_back(path);
    std::vector<std::filesystem::path> dirsToFsync;
    while (!dirsToEnumerate.empty()) {
        auto currentDir = dirsToEnumerate.back();
        dirsToEnumerate.pop_back();
        for (auto & entry : DirectoryIterator(currentDir)) {
            auto st = entry.symlink_status();
            if (std::filesystem::is_directory(st)) {
                dirsToEnumerate.emplace_back(entry.path());
            } else if (std::filesystem::is_regular_file(st)) {
                AutoCloseFD fd = toDescriptor(open(entry.path().string().c_str(), O_RDONLY, 0));
                if (!fd)
                    throw SysError("opening file %1%", PathFmt(entry.path()));
                fd.fsync();
            }
        }
        dirsToFsync.emplace_back(std::move(currentDir));
    }

    /* Fsync all the directories. */
    for (auto dir = dirsToFsync.rbegin(); dir != dirsToFsync.rend(); ++dir) {
        AutoCloseFD fd = toDescriptor(open(dir->string().c_str(), O_RDONLY, 0));
        if (!fd)
            throw SysError("opening directory %1%", PathFmt(*dir));
        fd.fsync();
    }
}

void createDir(const Path & path, mode_t mode)
{
    if (mkdir(
            path.c_str()
#ifndef _WIN32
                ,
            mode
#endif
            )
        == -1)
        throw SysError("creating directory '%1%'", path);
}

void createDirs(const std::filesystem::path & path)
{
    try {
        std::filesystem::create_directories(path);
    } catch (std::filesystem::filesystem_error & e) {
        throw SystemError(e.code(), "creating directory '%1%'", path.string());
    }
}

//////////////////////////////////////////////////////////////////////

AutoDelete::AutoDelete()
    : del{false}
    , recursive(false)
{
}

AutoDelete::AutoDelete(const std::filesystem::path & p, bool recursive)
    : _path(p)
    , del(true)
    , recursive(recursive)
{
}

void AutoDelete::deletePath()
{
    if (del) {
        if (recursive)
            nix::deletePath(_path);
        else
            std::filesystem::remove(_path);
        cancel();
    }
}

AutoDelete::~AutoDelete()
{
    try {
        deletePath();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

void AutoDelete::cancel() noexcept
{
    del = false;
}

//////////////////////////////////////////////////////////////////////

#ifdef __FreeBSD__
AutoUnmount::AutoUnmount()
    : del{false}
{
}

AutoUnmount::AutoUnmount(const std::filesystem::path & p)
    : path(p)
    , del(true)
{
}

AutoUnmount::~AutoUnmount()
{
    try {
        unmount();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

void AutoUnmount::cancel() noexcept
{
    del = false;
}

void AutoUnmount::unmount()
{
    if (del) {
        if (::unmount(path.c_str(), 0) < 0) {
            throw SysError("Failed to unmount path %1%", PathFmt(path));
        }
    }
    cancel();
}
#endif

//////////////////////////////////////////////////////////////////////

std::filesystem::path createTempDir(const std::filesystem::path & tmpRoot, const std::string & prefix, mode_t mode)
{
    while (1) {
        checkInterrupt();
        std::filesystem::path tmpDir = makeTempPath(tmpRoot, prefix);
        if (mkdir(
                tmpDir.string().c_str()
#ifndef _WIN32 // TODO abstract mkdir perms for Windows
                    ,
                mode
#endif
                )
            == 0) {
#ifdef __FreeBSD__
            /* Explicitly set the group of the directory.  This is to
               work around around problems caused by BSD's group
               ownership semantics (directories inherit the group of
               the parent).  For instance, the group of /tmp on
               FreeBSD is "wheel", so all directories created in /tmp
               will be owned by "wheel"; but if the user is not in
               "wheel", then "tar" will fail to unpack archives that
               have the setgid bit set on directories. */
            if (chown(tmpDir.c_str(), (uid_t) -1, getegid()) != 0)
                throw SysError("setting group of directory %1%", PathFmt(tmpDir));
#endif
            return tmpDir;
        }
        if (errno != EEXIST)
            throw SysError("creating directory %1%", PathFmt(tmpDir));
    }
}

AutoCloseFD createAnonymousTempFile()
{
    AutoCloseFD fd;

#ifdef _WIN32
    auto path = makeTempPath(defaultTempDir(), "nix-anonymous");
    fd = CreateFileW(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        /*dwShareMode=*/0,
        /*lpSecurityAttributes=*/nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
        /*hTemplateFile=*/nullptr);
    if (!fd)
        throw windows::WinError("creating temporary file %1%", PathFmt(path));
#else
#  ifdef O_TMPFILE
    static std::atomic_flag tmpfileUnsupported{};
    if (!tmpfileUnsupported.test()) /* Try with O_TMPFILE first. */ {
        /* Use O_EXCL, because the file is never supposed to be linked into filesystem. */
        fd = ::open(defaultTempDir().c_str(), O_TMPFILE | O_CLOEXEC | O_RDWR | O_EXCL, S_IWUSR | S_IRUSR);
        if (!fd) {
            /* Not supported by the filesystem or the kernel. */
            if (errno == EOPNOTSUPP || errno == EISDIR)
                tmpfileUnsupported.test_and_set(); /* Set flag and fall through to createTempFile. */
            else
                throw SysError("creating anonymous temporary file");
        } else {
            return fd; /* Successfully created. */
        }
    }
#  endif
    auto [fd2, path] = createTempFile("nix-anonymous");
    if (!fd2)
        throw SysError("creating temporary file '%s'", path);
    fd = std::move(fd2);
    unlink(requireCString(path)); /* We only care about the file descriptor. */
#endif

    return fd;
}

std::pair<AutoCloseFD, Path> createTempFile(const Path & prefix)
{
    Path tmpl(defaultTempDir().string() + "/" + prefix + ".XXXXXX");
    // Strictly speaking, this is UB, but who cares...
    // FIXME: use O_TMPFILE.
    AutoCloseFD fd = toDescriptor(mkstemp((char *) tmpl.c_str()));
    if (!fd)
        throw SysError("creating temporary file '%s'", tmpl);
#ifndef _WIN32
    unix::closeOnExec(fd.get());
#endif
    return {std::move(fd), tmpl};
}

std::filesystem::path makeTempPath(const std::filesystem::path & root, const std::string & suffix)
{
    // start the counter at a random value to minimize issues with preexisting temp paths
    static std::atomic<uint32_t> counter(std::random_device{}());
    auto tmpRoot = canonPath(root.empty() ? defaultTempDir().string() : root.string(), true);
    return fmt("%1%/%2%-%3%-%4%", tmpRoot, suffix, getpid(), counter.fetch_add(1, std::memory_order_relaxed));
}

void createSymlink(const Path & target, const Path & link)
{
    std::error_code ec;
    std::filesystem::create_symlink(target, link, ec);
    if (ec)
        throw SysError(ec.value(), "creating symlink '%1%' -> '%2%'", link, target);
}

void replaceSymlink(const std::filesystem::path & target, const std::filesystem::path & link)
{
    for (unsigned int n = 0; true; n++) {
        auto tmp = link.parent_path() / std::filesystem::path{fmt(".%d_%s", n, link.filename().string())};
        tmp = tmp.lexically_normal();

        try {
            std::filesystem::create_symlink(target, tmp);
        } catch (std::filesystem::filesystem_error & e) {
            if (e.code() == std::errc::file_exists)
                continue;
            throw SystemError(e.code(), "creating symlink %1% -> %2%", PathFmt(tmp), PathFmt(target));
        }

        try {
            std::filesystem::rename(tmp, link);
        } catch (std::filesystem::filesystem_error & e) {
            if (e.code() == std::errc::file_exists)
                continue;
            throw SystemError(e.code(), "renaming %1% to %2%", PathFmt(tmp), PathFmt(link));
        }

        break;
    }
}

void setWriteTime(const std::filesystem::path & path, const PosixStat & st)
{
    setWriteTime(path, st.st_atime, st.st_mtime, S_ISLNK(st.st_mode));
}

void copyFile(const std::filesystem::path & from, const std::filesystem::path & to, bool andDelete)
{
    auto fromStatus = std::filesystem::symlink_status(from);

    // Mark the directory as writable so that we can delete its children
    if (andDelete && std::filesystem::is_directory(fromStatus)) {
        std::filesystem::permissions(
            from,
            std::filesystem::perms::owner_write,
            std::filesystem::perm_options::add | std::filesystem::perm_options::nofollow);
    }

    if (std::filesystem::is_symlink(fromStatus) || std::filesystem::is_regular_file(fromStatus)) {
        std::filesystem::copy(
            from, to, std::filesystem::copy_options::copy_symlinks | std::filesystem::copy_options::overwrite_existing);
    } else if (std::filesystem::is_directory(fromStatus)) {
        std::filesystem::create_directory(to);
        for (auto & entry : DirectoryIterator(from)) {
            copyFile(entry, to / entry.path().filename(), andDelete);
        }
    } else {
        throw Error("file %s has an unsupported type", PathFmt(from));
    }

    setWriteTime(to, lstat(from.string().c_str()));
    if (andDelete) {
        if (!std::filesystem::is_symlink(fromStatus))
            std::filesystem::permissions(
                from,
                std::filesystem::perms::owner_write,
                std::filesystem::perm_options::add | std::filesystem::perm_options::nofollow);
        std::filesystem::remove(from);
    }
}

void moveFile(const Path & oldName, const Path & newName)
{
    try {
        std::filesystem::rename(oldName, newName);
    } catch (std::filesystem::filesystem_error & e) {
        auto oldPath = std::filesystem::path(oldName);
        auto newPath = std::filesystem::path(newName);
        // For the move to be as atomic as possible, copy to a temporary
        // directory
        std::filesystem::path temp =
            createTempDir(os_string_to_string(PathViewNG{newPath.parent_path()}), "rename-tmp");
        Finally removeTemp = [&]() { std::filesystem::remove(temp); };
        auto tempCopyTarget = temp / "copy-target";
        if (e.code().value() == EXDEV) {
            std::filesystem::remove(newPath);
            warn("can’t rename %s as %s, copying instead", oldName, newName);
            copyFile(oldPath, tempCopyTarget, true);
            std::filesystem::rename(
                os_string_to_string(PathViewNG{tempCopyTarget}), os_string_to_string(PathViewNG{newPath}));
        }
    }
}

//////////////////////////////////////////////////////////////////////

bool isExecutableFileAmbient(const std::filesystem::path & exe)
{
    // Check file type, because directory being executable means
    // something completely different.
    // `is_regular_file` follows symlinks before checking.
    return std::filesystem::is_regular_file(exe)
           && access(
                  exe.string().c_str(),
#ifdef WIN32
                  0 // TODO do better
#else
                  X_OK
#endif
                  )
                  == 0;
}

std::filesystem::path makeParentCanonical(const std::filesystem::path & rawPath)
{
    std::filesystem::path path(absPath(rawPath));
    ;
    try {
        auto parent = path.parent_path();
        if (parent == path) {
            // `path` is a root directory => trivially canonical
            return parent;
        }
        return std::filesystem::canonical(parent) / path.filename();
    } catch (std::filesystem::filesystem_error & e) {
        throw SystemError(e.code(), "canonicalising parent path of %1%", PathFmt(path));
    }
}

void chmod(const std::filesystem::path & path, mode_t mode)
{
    if (
#ifdef _WIN32
        ::_wchmod
#else
        ::chmod
#endif
        (path.c_str(), mode)
        == -1)
        throw SysError("setting permissions on %s", PathFmt(path));
}

bool chmodIfNeeded(const std::filesystem::path & path, mode_t mode, mode_t mask)
{
    auto pathString = path.string();
    auto prevMode = lstat(pathString).st_mode;

    if (((prevMode ^ mode) & mask) == 0)
        return false;

    chmod(path, mode);
    return true;
}

} // namespace nix
