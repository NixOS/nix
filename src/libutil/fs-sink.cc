#include <fcntl.h>

#include "nix/util/error.hh"
#include "nix/util/config-global.hh"
#include "nix/util/file-system-at.hh"
#include "nix/util/fs-sink.hh"

#ifdef _WIN32
#  include <fileapi.h>
#  include "nix/util/file-path.hh"
#endif

#include "util-config-private.hh"

namespace nix {

void copyRecursive(SourceAccessor & accessor, const CanonPath & from, FileSystemObjectSink & sink, const CanonPath & to)
{
    auto stat = accessor.lstat(from);

    switch (stat.type) {
    case SourceAccessor::tSymlink: {
        sink.createSymlink(to, accessor.readLink(from));
        break;
    }

    case SourceAccessor::tRegular: {
        sink.createRegularFile(to, [&](CreateRegularFileSink & crf) {
            if (stat.isExecutable)
                crf.isExecutable();
            accessor.readFile(from, crf, [&](uint64_t size) { crf.preallocateContents(size); });
        });
        break;
    }

    case SourceAccessor::tDirectory: {
        sink.createDirectory(to, [&](FileSystemObjectSink & dirSink, const CanonPath & relDirPathTo) {
            accessor.readDirectory(from, [&](SourceAccessor & subdirAccessor, const CanonPath & relDirPathFrom) {
                for (auto & [name, _] : subdirAccessor.readDirectory(relDirPathFrom)) {
                    copyRecursive(subdirAccessor, relDirPathFrom / name, dirSink, relDirPathTo / name);
                }
            });
        });
        break;
    }

    case SourceAccessor::tChar:
    case SourceAccessor::tBlock:
    case SourceAccessor::tSocket:
    case SourceAccessor::tFifo:
    case SourceAccessor::tUnknown:
    default:
        throw Error("file '%1%' has an unsupported type of %2%", from, stat.typeString());
    }
}

struct RestoreSinkSettings : Config
{
    Setting<bool> preallocateContents{
        this, false, "preallocate-contents", "Whether to preallocate files when writing objects with known size."};
};

static RestoreSinkSettings restoreSinkSettings;

static GlobalConfig::Register r1(&restoreSinkSettings);

static std::filesystem::path append(const std::filesystem::path & src, const CanonPath & path)
{
    auto dst = src;
    if (!path.rel().empty())
        dst /= path.rel();
    return dst;
}

/**
 * Return a descriptor and single-component name suitable for
 * `*at` operations. The returned `CanonPath` is always a pure
 * name (no slashes). The `Descriptor` is the fd to use. The
 * `AutoCloseFD` keeps it alive when it was opened temporarily
 * (for multi-component paths); otherwise it is empty and the
 * `Descriptor` borrows from `dirFd`. When `dirFd` is not set,
 * temporarily opens the parent of `dstPath`.
 */
static std::tuple<AutoCloseFD, Descriptor, OsFilename>
getParentFdAndName(Descriptor dirFd, const std::filesystem::path & dstPath, const CanonPath & path)
{
    if (dirFd != INVALID_DESCRIPTOR) {
        /* dirFd is the root of the restore tree, which means we already created
           a root directory, which means that path must be relative (i.e. not
           root) within it. */
        assert(!path.isRoot());
        auto parent = path.parent();
        auto name = OsFilename{std::filesystem::path{std::string{*path.baseName()}}};
        if (parent->isRoot())
            return {AutoCloseFD{}, dirFd, std::move(name)};
        auto parentFd = openFileEnsureBeneathNoSymlinks(
            dirFd,
            *parent,
#ifdef _WIN32
            FILE_READ_ATTRIBUTES | SYNCHRONIZE,
            FILE_DIRECTORY_FILE
#else
            O_RDONLY | O_DIRECTORY | O_CLOEXEC,
            0
#endif
        );
        if (!parentFd)
            throw SysError("opening parent directory of %s", PathFmt(append(dstPath, path)));
        auto fd = parentFd.get();
        return {std::move(parentFd), fd, std::move(name)};
    }

    /* Without dirFd, we're creating the root entry itself, so path
       must be root. If it's not, someone forgot to create the root
       directory first. */
    auto p = append(dstPath, path);
    if (!path.isRoot())
        throw Error("cannot create non-root path %s without a root directory", PathFmt(p));
    if (p.empty())
        throw Error("restore destination path is empty");
    auto filename = p.filename();
    if (filename == "." || filename == "..")
        throw Error("restore destination %s ends in %s, which is not a valid filename", PathFmt(p), PathFmt(filename));
    auto parentPath = p.parent_path();
    /* Relative path with no directory component (e.g. "out") —
       the parent is the current working directory. Open it so we
       hold a stable reference in case something else in the process
       changes the working directory mid-unpack. */
    if (parentPath.empty())
        parentPath = ".";
    auto parentFd = openDirectory(parentPath, FinalSymlink::Follow);
    if (!parentFd)
        throw NativeSysError("opening parent directory of %s", PathFmt(p));
    auto fd = parentFd.get();
    return {std::move(parentFd), fd, OsFilename{p.filename()}};
}

void RestoreSink::createDirectory(const CanonPath & path, DirectoryCreatedCallback callback)
{
    if (path.isRoot()) {
        createDirectory(path);
        callback(*this, path);
        return;
    }

    createDirectory(path);
    assert(dirFd); // If that's not true the above call must have thrown an exception.

    RestoreSink dirSink{startFsync};
    dirSink.dstPath = append(dstPath, path);
    dirSink.dirFd = openFileEnsureBeneathNoSymlinks(
        dirFd.get(),
        path,
#ifdef _WIN32
        FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_DIRECTORY_FILE
#else
        O_RDONLY | O_DIRECTORY | O_CLOEXEC,
        0
#endif
    );

    if (!dirSink.dirFd)
        throw SysError("opening directory %s", PathFmt(dirSink.dstPath));

    callback(dirSink, CanonPath::root);
}

void RestoreSink::createDirectory(const CanonPath & path)
{
    if (dirFd && path.isRoot())
        throw Error("path %s already exists", PathFmt(append(dstPath, path)));

    auto [_parentFd, fd, name] = getParentFdAndName(dirFd.get(), dstPath, path);

    if (auto result = openDirectoryAt(fd, OsCanonPath{name}, true); !result) {
        throw SystemError(result.error(), "creating directory %s", PathFmt(append(dstPath, path)));
    }

    if (!dirFd) {
        /* Open directory for further *at operations relative to the sink root
           directory. */
        dirFd = openFileEnsureBeneathNoSymlinks(
            fd,
            OsCanonPath{name},
#ifdef _WIN32
            FILE_READ_ATTRIBUTES | SYNCHRONIZE,
            FILE_DIRECTORY_FILE
#else
            O_RDONLY | O_DIRECTORY | O_CLOEXEC,
            0
#endif
        );
        if (!dirFd)
            throw NativeSysError("opening directory %s", PathFmt(append(dstPath, path)));
    }
};

struct RestoreRegularFile : CreateRegularFileSink, FdSink
{
    AutoCloseFD fd;
    bool startFsync = false;

    RestoreRegularFile(bool startFSync_, AutoCloseFD fd_)
        : FdSink(fd_.get())
        , fd(std::move(fd_))
        , startFsync(startFSync_)
    {
    }

    ~RestoreRegularFile()
    {
        /* Flush the sink before FdSink destructor has a chance to run and we've
           closed the file descriptor. */
        if (fd) {
            try {
                FdSink::flush();
            } catch (...) {
                ignoreExceptionInDestructor();
            }
        }

        /* Initiate an fsync operation without waiting for the
           result. The real fsync should be run before registering a
           store path, but this is a performance optimization to allow
           the disk write to start early. */
        if (fd && startFsync)
            fd.startFsync();
    }

    void isExecutable() override;
    void preallocateContents(uint64_t size) override;
};

void RestoreSink::createRegularFile(const CanonPath & path, fun<void(CreateRegularFileSink &)> func)
{
    auto [_parentFd, fd, name] = getParentFdAndName(dirFd.get(), dstPath, path);
    auto crf = RestoreRegularFile(
        startFsync,
        openFileEnsureBeneathNoSymlinks(
            fd,
            OsCanonPath{name},
#ifdef _WIN32
            FILE_WRITE_DATA | SYNCHRONIZE,
            0,
            FILE_CREATE
#else
            O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC,
            0666
#endif
            ));
    if (!crf.fd)
        throw NativeSysError("creating file %s", PathFmt(append(dstPath, path)));
    func(crf);
    crf.flush();
}

void RestoreRegularFile::isExecutable()
{
    // Windows doesn't have a notion of executable file permissions we
    // care about here, right?
#ifndef _WIN32
    auto st = nix::fstat(fd.get());
    if (fchmod(fd.get(), st.st_mode | (S_IXUSR | S_IXGRP | S_IXOTH)) == -1)
        throw SysError("fchmod");
#endif
}

void RestoreRegularFile::preallocateContents(uint64_t len)
{
    if (!restoreSinkSettings.preallocateContents)
        return;

#if HAVE_POSIX_FALLOCATE
    if (len) {
        errno = posix_fallocate(fd.get(), 0, len);
        /* Note that EINVAL may indicate that the underlying
           filesystem doesn't support preallocation (e.g. on
           OpenSolaris).  Since preallocation is just an
           optimisation, ignore it. */
        if (errno && errno != EINVAL && errno != EOPNOTSUPP && errno != ENOSYS)
            throw SysError("preallocating file of %1% bytes", len);
    }
#endif
}

void RestoreSink::createSymlink(const CanonPath & path, const std::string & target)
{
    auto [_parentFd, fd, name] = getParentFdAndName(dirFd.get(), dstPath, path);
    createUnknownSymlinkAt(fd, OsCanonPath{name}, string_to_os_string(target));
}

void RegularFileSink::createRegularFile(const CanonPath & path, fun<void(CreateRegularFileSink &)> func)
{
    struct CRF : CreateRegularFileSink
    {
        RegularFileSink & back;

        CRF(RegularFileSink & back)
            : back(back)
        {
        }

        void operator()(std::string_view data) override
        {
            back.sink(data);
        }

        void isExecutable() override {}
    } crf{*this};

    func(crf);
}

void NullFileSystemObjectSink::createRegularFile(const CanonPath & path, fun<void(CreateRegularFileSink &)> func)
{
    struct : CreateRegularFileSink
    {
        void operator()(std::string_view data) override {}

        void isExecutable() override {}
    } crf;

    crf.skipContents = true;

    // Even though `NullFileSystemObjectSink` doesn't do anything, it's important
    // that we call the function, to e.g. advance the parser using this
    // sink.
    func(crf);
}

} // namespace nix
