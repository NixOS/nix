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

void copyRecursive(SourceAccessor & accessor, const CanonPath & from, FileSystemObjectSink & sink)
{
    auto stat = accessor.lstat(from);

    switch (stat.type) {
    case SourceAccessor::tSymlink: {
        sink.createSymlink(accessor.readLink(from));
        break;
    }

    case SourceAccessor::tRegular: {
        sink.createRegularFile(stat.isExecutable, [&](FileSystemObjectSink::OnRegularFile & crf) {
            accessor.readFile(from, crf, [&](uint64_t size) { crf.preallocateContents(size); });
        });
        break;
    }

    case SourceAccessor::tDirectory: {
        sink.createDirectory([&](FileSystemObjectSink::OnDirectory & dirSink) {
            for (auto & [name, _] : accessor.readDirectory(from)) {
                dirSink.createChild(
                    name, [&](FileSystemObjectSink & childSink) { copyRecursive(accessor, from / name, childSink); });
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

void RestoreSink::Directory::createChild(std::string_view name, ChildCreatedCallback callback)
{
    RestoreSink childSink{back.startFsync};
    childSink.parentPath = dirPath;
    childSink.childName = name;
    childSink.dirFd = dirFd.get();
    callback(childSink);
}

void RestoreSink::createDirectory(DirectoryCreatedCallback callback)
{
    auto dstPath = parentPath / childName;
#ifndef _WIN32
    if (dirFd != INVALID_DESCRIPTOR) {
        if (::mkdirat(dirFd, childName.c_str(), 0777) == -1)
            throw SysError("creating directory %s", PathFmt(dstPath));
    } else
#endif
    {
        if (!std::filesystem::create_directory(dstPath))
            throw Error("path %s already exists", PathFmt(dstPath));
    }

    AutoCloseFD newDirFd;
#ifndef _WIN32
    if (dirFd != INVALID_DESCRIPTOR) {
        newDirFd = AutoCloseFD{::openat(dirFd, childName.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
    } else {
        newDirFd = AutoCloseFD{::open(dstPath.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
    }
    if (!newDirFd)
        throw SysError("opening directory %s", PathFmt(dstPath));
#endif

    Directory dir{*this, dstPath, std::move(newDirFd)};
    callback(dir);
}

struct RestoreRegularFile : FileSystemObjectSink::OnRegularFile, FdSink
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

    void preallocateContents(uint64_t size) override;
};

void RestoreSink::createRegularFile(bool isExecutable, RegularFileCreatedCallback func)
{
    auto dstPath = parentPath / childName;
    auto crf = RestoreRegularFile(
        startFsync,
#ifdef _WIN32
        CreateFileW(
            dstPath.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            NULL)
#else
        [&]() {
            /* O_EXCL together with O_CREAT ensures symbolic links in the last
               component are not followed. */
            constexpr int flags = O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC;
            if (dirFd == INVALID_DESCRIPTOR)
                return AutoCloseFD{::open(dstPath.c_str(), flags, 0666)};
            return AutoCloseFD{::openat(dirFd, childName.c_str(), flags, 0666)};
        }()
#endif
    );
    if (!crf.fd)
        throw NativeSysError("creating file %1%", PathFmt(dstPath));
    func(crf);
    crf.flush();
    // Windows doesn't have a notion of executable file permissions we
    // care about here, right?
#ifndef _WIN32
    if (isExecutable) {
        auto st = nix::fstat(crf.fd.get());
        if (fchmod(crf.fd.get(), st.st_mode | (S_IXUSR | S_IXGRP | S_IXOTH)) == -1)
            throw SysError("fchmod");
    }
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

void RestoreSink::createSymlink(const std::string & target)
{
    auto dstPath = parentPath / childName;
#ifndef _WIN32
    if (dirFd != INVALID_DESCRIPTOR) {
        if (::symlinkat(requireCString(target), dirFd, childName.c_str()) == -1)
            throw SysError("creating symlink from %1% -> '%2%'", PathFmt(dstPath), target);
        return;
    }
#endif
    nix::createSymlink(target, dstPath.string());
}

void RegularFileSink::createRegularFile(bool isExecutable, RegularFileCreatedCallback func)
{
    struct CRF : OnRegularFile
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
    } crf{*this};

    func(crf);
}

void NullFileSystemObjectSink::createDirectory(DirectoryCreatedCallback func)
{
    struct : OnDirectory
    {
        void createChild(std::string_view, ChildCreatedCallback callback) override
        {
            NullFileSystemObjectSink childSink;
            callback(childSink);
        }
    } dir;

    func(dir);
}

void NullFileSystemObjectSink::createRegularFile(bool isExecutable, RegularFileCreatedCallback func)
{
    struct : OnRegularFile
    {
        void operator()(std::string_view data) override {}
    } crf;

    crf.skipContents = true;

    // Even though `NullFileSystemObjectSink` doesn't do anything, it's important
    // that we call the function, to e.g. advance the parser using this
    // sink.
    func(crf);
}

} // namespace nix
