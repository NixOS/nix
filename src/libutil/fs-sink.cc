#include <fcntl.h>

#include "nix/util/error.hh"
#include "nix/util/config-global.hh"
#include "nix/util/file-system-at.hh"
#include "nix/util/fs-sink.hh"
#include "nix/util/util.hh"

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

void RestoreSink::createDirectory(const CanonPath & path, DirectoryCreatedCallback callback)
{
    if (path.isRoot()) {
        createDirectory(path);
        callback(*this, path);
        return;
    }

    createDirectory(path);
    // After createDirectory, destination is always AutoCloseFD
    auto * dirFd = std::get_if<AutoCloseFD>(&destination.raw);
    assert(dirFd);

    auto childDirFd = openFileEnsureBeneathNoSymlinks(
        dirFd->get(),
        path,
#ifdef _WIN32
        FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_DIRECTORY_FILE
#else
        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC,
        0
#endif
    );

    if (!childDirFd)
        throw NativeSysError(
            [&] { return HintFmt("opening directory %s", PathFmt(destination.toPath() / path.rel())); });

    RestoreSink dirSink{std::move(childDirFd), startFsync};
    callback(dirSink, CanonPath::root);
}

void RestoreSink::createDirectory(const CanonPath & path)
{
    std::visit(
        overloaded{
            [&](DescriptorDestination::Parent & parent) {
                if (!path.isRoot())
                    throw Error(
                        "cannot create %s because %s doesn't exist yet",
                        PathFmt(destination.toPath() / path.rel()),
                        PathFmt(destination.toPath()));

                auto name = CanonPath{parent.name.string()};
                try {
                    createDirectoryAt(parent.fd.get(), name);
                } catch (SystemError & e) {
                    if (e.is(std::errc::file_exists))
                        throw Error("path %s already exists", PathFmt(destination.toPath()));
                    throw;
                }

                /* Open directory for further *at operations relative to the sink root directory. */
                auto dirFd = openFileEnsureBeneathNoSymlinks(
                    parent.fd.get(),
                    name,
#ifdef _WIN32
                    FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                    FILE_DIRECTORY_FILE
#else
                    O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC,
                    0
#endif
                );
                if (!dirFd)
                    throw NativeSysError(
                        [&] { return HintFmt("opening directory %s", PathFmt(destination.toPath())); });
                destination = std::move(dirFd);
            },
            [&](const AutoCloseFD & dirFd) {
                if (path.isRoot())
                    /* Trying to create a directory that we already have a file descriptor for. */
                    throw Error("path %s already exists", PathFmt(destination.toPath()));

                createDirectoryAt(dirFd.get(), path);
            },
        },
        destination.raw);
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
    auto crf = RestoreRegularFile(
        startFsync,
        std::visit(
            overloaded{
                [&](const DescriptorDestination::Parent & parent) {
                    assert(path.isRoot());

                    return openFileEnsureBeneathNoSymlinks(
                        parent.fd.get(),
                        CanonPath{parent.name.string()},
#ifdef _WIN32
                        GENERIC_READ | GENERIC_WRITE,
                        FILE_NON_DIRECTORY_FILE,
                        FILE_CREATE
#else
                        O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC,
                        0666
#endif
                    );
                },
                [&](const AutoCloseFD & dirFd) {
                    return openFileEnsureBeneathNoSymlinks(
                        dirFd.get(),
                        path,
#ifdef _WIN32
                        GENERIC_READ | GENERIC_WRITE,
                        FILE_NON_DIRECTORY_FILE,
                        FILE_CREATE
#else
                        O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC,
                        0666
#endif
                    );
                },
            },
            destination.raw));
    if (!crf.fd)
        throw NativeSysError([&] { return HintFmt("creating file %s", PathFmt(destination.toPath() / path.rel())); });
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
    std::visit(
        overloaded{
            [&](const DescriptorDestination::Parent & parent) {
                assert(path.isRoot());

                createUnknownSymlinkAt(parent.fd.get(), CanonPath{parent.name.string()}, string_to_os_string(target));
            },
            [&](const AutoCloseFD & dirFd) { createUnknownSymlinkAt(dirFd.get(), path, string_to_os_string(target)); },
        },
        destination.raw);
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
