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

/**
 * Return a descriptor and single-component name suitable for
 * `*at` operations. The returned `OsFilename` is always a pure
 * name (no slashes). The `Descriptor` is the fd to use. The
 * `AutoCloseFD` keeps it alive when it was opened temporarily
 * (for multi-component paths); otherwise it is empty and the
 * `Descriptor` borrows from `dirFd`.
 */
static std::tuple<AutoCloseFD, Descriptor, OsFilename> getParentFdAndName(RestoreSink & sink, const CanonPath & path)
{
    if (auto * parent = std::get_if<RestoreSink::DirFdParent>(&sink.dirFdKind)) {
        /* dirFd is the parent directory. The root entry's name comes
           from the variant. path must be root since we haven't
           created the root directory yet. */
        if (!path.isRoot())
            throw Error("cannot create non-root path %s without a root directory", path);
        return {AutoCloseFD{}, sink.dirFd.get(), parent->name};
    }

    /* dirFd is the root of the restore tree, so path must be
       relative (i.e. not root) within it. */
    assert(!path.isRoot());
    auto parent = path.parent();
    if (parent->isRoot())
        return {AutoCloseFD{}, sink.dirFd.get(), OsFilename{std::filesystem::path(std::string{*path.baseName()})}};

    /* Multi-component path: open intermediate directories to reach
       the immediate parent. The returned AutoCloseFD keeps it alive. */
    auto parentFd = openFileEnsureBeneathNoSymlinks(
        sink.dirFd.get(),
        std::filesystem::path(parent->rel()),
#ifdef _WIN32
        FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_DIRECTORY_FILE
#else
        O_RDONLY | O_DIRECTORY | O_CLOEXEC,
        0
#endif
    );
    if (!parentFd)
        throw SysError("opening parent directory of %s", PathFmt(descriptorToPath(sink.dirFd.get()) / path.rel()));
    auto fd = parentFd.get();
    return {std::move(parentFd), fd, OsFilename{std::filesystem::path(std::string{*path.baseName()})}};
}

void RestoreSink::createDirectory(const CanonPath & path, DirectoryCreatedCallback callback)
{
    if (path.isRoot()) {
        createDirectory(path);
        callback(*this, path);
        return;
    }

    createDirectory(path);
    assert(
        std::holds_alternative<DirFdRoot>(
            dirFdKind)); // If that's not true the above call must have thrown an exception.

    auto subDirFd = openFileEnsureBeneathNoSymlinks(
        dirFd.get(),
        std::filesystem::path(path.rel()),
#ifdef _WIN32
        FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_DIRECTORY_FILE
#else
        O_RDONLY | O_DIRECTORY | O_CLOEXEC,
        0
#endif
    );

    if (!subDirFd)
        throw SysError("opening directory %s", PathFmt(descriptorToPath(dirFd.get()) / path.rel()));

    RestoreSink dirSink{DirFdRoot{}, std::move(subDirFd), startFsync};

    callback(dirSink, CanonPath::root);
}

void RestoreSink::createDirectory(const CanonPath & path)
{
    if (std::holds_alternative<DirFdRoot>(dirFdKind) && path.isRoot())
        /* Trying to create a directory that we already have a file descriptor for. */
        throw Error("path %s already exists", PathFmt(descriptorToPath(dirFd.get())));

    auto [_parentFd, fd, name] = getParentFdAndName(*this, path);

    if (auto result = openDirectoryAt(fd, name, true); !result) {
        auto ec = result.error();
        auto fullPath = descriptorToPath(fd) / name.path();
        if (ec == std::errc::not_a_directory || ec == std::errc::too_many_symbolic_link_levels)
            throw SymlinkNotAllowed(fullPath);
        throw SystemError(ec, "creating directory %s", PathFmt(fullPath));
    }

    if (std::holds_alternative<DirFdParent>(dirFdKind)) {
        /* getParentFdAndName throws if path is not root when dirFdKind is Parent. */
        assert(path.isRoot());
        /* Open the newly created root directory for further *at operations.
           Use a temp because `fd` borrows from `this->dirFd` and the
           assignment would close it before the error message could use it. */
        auto newDirFd = openFileEnsureBeneathNoSymlinks(
            fd,
            name,
#ifdef _WIN32
            FILE_READ_ATTRIBUTES | SYNCHRONIZE,
            FILE_DIRECTORY_FILE
#else
            O_RDONLY | O_DIRECTORY | O_CLOEXEC,
            0
#endif
        );
        if (!newDirFd)
            throw SysError("opening directory %s", PathFmt(descriptorToPath(fd) / name.path()));
        dirFd = std::move(newDirFd);
        dirFdKind = DirFdRoot{};
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
    auto [_parentFd, parentDescriptor, name] = getParentFdAndName(*this, path);
    auto fd = openFileEnsureBeneathNoSymlinks(
        parentDescriptor,
        name,
#ifdef _WIN32
        FILE_WRITE_DATA | SYNCHRONIZE,
        0,
        FILE_CREATE
#else
        O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC,
        0666
#endif
    );
    if (!fd)
        throw NativeSysError("creating file %s", PathFmt(descriptorToPath(dirFd.get()) / path.rel()));

    auto crf = RestoreRegularFile(startFsync, std::move(fd));
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
    auto [_parentFd, fd, name] = getParentFdAndName(*this, path);
    createUnknownSymlinkAt(fd, name, string_to_os_string(target));
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
