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
        sink.createDirectory(to, [&](FileSystemObjectSink & dirSink, const CanonPath & relDirPath) {
            for (auto & [name, _] : accessor.readDirectory(from)) {
                copyRecursive(accessor, from / name, dirSink, relDirPath / name);
            }
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
        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC,
        0
#endif
    );

    if (!dirSink.dirFd)
        throw SysError("opening directory %s", PathFmt(dirSink.dstPath));

    callback(dirSink, CanonPath::root);
}

void RestoreSink::createDirectory(const CanonPath & path)
{
    auto p = append(dstPath, path);

    if (dirFd) {
        if (path.isRoot())
            /* Trying to create a directory that we already have a file descriptor for. */
            throw Error("path %s already exists", PathFmt(p));

        createDirectoryAt(dirFd.get(), path);
        return;
    }

    if (!std::filesystem::create_directory(p))
        throw Error("path '%s' already exists", p.string());

    if (path.isRoot()) {
        assert(!dirFd); // Handled above

        /* Open directory for further *at operations relative to the sink root
           directory. */
        dirFd = openDirectory(p, false);
    }
}

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

void RestoreSink::createRegularFile(const CanonPath & path, std::function<void(CreateRegularFileSink &)> func)
{
    auto p = append(dstPath, path);

    auto crf = RestoreRegularFile(startFsync, [&]() -> Descriptor {
        AutoCloseFD parentFd;
        if (!dirFd) {
            assert(path.isRoot());
            assert(p.has_parent_path());
            parentFd = openDirectory(p.parent_path());
        }
        return openFileEnsureBeneathNoSymlinks(
            dirFd ? dirFd.get() : parentFd.get(),
            dirFd ? path : CanonPath::root / p.filename().string(),
#ifdef _WIN32
            GENERIC_READ | GENERIC_WRITE,
            FILE_NON_DIRECTORY_FILE,
            FILE_CREATE
#else
                O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC,
                0666
#endif
        );
    }());
    if (!crf.fd)
        throw NativeSysError("creating file %1%", PathFmt(p));
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
    auto p = append(dstPath, path);
    AutoCloseFD parentFd;
    if (!dirFd) {
        assert(path.isRoot());
        assert(p.has_parent_path());
        parentFd = openDirectory(p.parent_path());
    }
    createSymlinkAt(
        dirFd ? dirFd.get() : parentFd.get(),
        dirFd ? path : CanonPath::root / p.filename().string(),
        string_to_os_string(target));
}

void RegularFileSink::createRegularFile(const CanonPath & path, std::function<void(CreateRegularFileSink &)> func)
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

void NullFileSystemObjectSink::createRegularFile(
    const CanonPath & path, std::function<void(CreateRegularFileSink &)> func)
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
