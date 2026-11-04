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

void FileSystemObjectSink::anchor() {}

void NullFileSystemObjectSink::anchor() {}

void RegularFileSink::anchor() {}

void RestoreSink::anchor() {}

void FileSystemObjectSink::OnRegularFile::anchor() {}

void FileSystemObjectSink::OnDirectory::anchor() {}

void RestoreSinkHooks::anchor() {}

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
            accessor.readDirectory(from, [&](SourceAccessor & subdirAccessor, const CanonPath & relDirPathFrom) {
                for (auto & [name, _] : subdirAccessor.readDirectory(relDirPathFrom)) {
                    dirSink.createChild(name, [&](FileSystemObjectSink & childSink) {
                        copyRecursive(subdirAccessor, relDirPathFrom / name, childSink);
                    });
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

namespace {

struct RestoreSinkSettings : Config
{
    Setting<bool> preallocateContents{
        this, false, "preallocate-contents", "Whether to preallocate files when writing objects with known size."};
};

static RestoreSinkSettings restoreSinkSettings;

static GlobalConfig::Register r1(&restoreSinkSettings);

} // namespace

RestoreSink RestoreSink::forPath(const std::filesystem::path & dstPath, bool startFsync, RestoreSinkHooks * hooks)
{
    if (dstPath.empty())
        throw Error("restore destination path is empty");
    auto name = [&] {
        try {
            return OsFilename::fromPathThrowing(dstPath.filename());
        } catch (Error & e) {
            e.addTrace({}, "while checking restore destination %s", PathFmt(dstPath));
            throw;
        }
    }();
    auto parentPath = dstPath.parent_path();
    /* Relative path with no directory component (e.g. "out") —
       the parent is the current working directory. Open it so we
       hold a stable reference in case something else in the process
       changes the working directory mid-unpack. */
    if (parentPath.empty())
        parentPath = ".";
    auto parentFd = openDirectory(parentPath, FinalSymlink::Follow);
    if (!parentFd)
        throw NativeSysError("opening parent directory of %s", PathFmt(dstPath));
    RestoreSink sink{parentFd.get(), std::move(name), startFsync, hooks};
    sink.ownedParentDir = std::move(parentFd);
    return sink;
}

void RestoreSink::Directory::createChild(std::string_view name, ChildCreatedCallback callback)
{
    /* Checked, not asserted: NAR and other sink inputs only exclude `/`,
       so a name may contain `\` or a drive prefix, which on Windows is
       not a single filename. */
    RestoreSink childSink{directory.get(), OsFilename::fromPathThrowing(name), startFsync, hooks};
    callback(childSink);
}

void RestoreSink::createDirectory(DirectoryCreatedCallback callback)
{
    auto result = createDirectoryAt(parentDir, name);
    if (!result)
        throw SystemError(result.error(), "creating directory %s", PathFmt(descriptorToPath(parentDir) / name.path()));

    /* Creating it may have opened it too; see `createDirectoryAt`. */
    auto dirFd = std::move(result.value());

    if (!dirFd) {
        /* Unfortunately not atomic with creating it; see `createDirectoryAt`. */
        dirFd = openFileEnsureBeneathNoSymlinks(
            parentDir,
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
            throw NativeSysError(
                [&] { return HintFmt("opening directory %s", PathFmt(descriptorToPath(parentDir) / name.path())); });
    }

    Directory dir{std::move(dirFd), startFsync, hooks};
    callback(dir);
    if (hooks)
        hooks->directoryDone(dir.directory.get());
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
        isRegularFile = true; /* Hint for copying contents via CoW copy_file_range. */
    }

    void anchor() override;

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

    void preallocateContents(uint64_t size) override;
};

void RestoreRegularFile::anchor() {}

void RestoreSink::createRegularFile(bool isExecutable, RegularFileCreatedCallback func)
{
    auto crf = RestoreRegularFile(
        startFsync,
        openFileEnsureBeneathNoSymlinks(
            parentDir,
            OsCanonPath{name},
#ifdef _WIN32
            FILE_WRITE_DATA | SYNCHRONIZE,
            0,
            FILE_CREATE
#else
            /* O_EXCL together with O_CREAT ensures symbolic links in the last
               component are not followed. */
            O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC,
            0666
#endif
            ));
    if (!crf.fd)
        throw NativeSysError(
            [&] { return HintFmt("creating file %s", PathFmt(descriptorToPath(parentDir) / name.path())); });
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
    if (hooks)
        hooks->regularFileCreated(crf.fd.get(), isExecutable);
}

void RestoreRegularFile::preallocateContents(uint64_t len)
{
    if (!restoreSinkSettings.preallocateContents)
        return;

#if HAVE_POSIX_FALLOCATE
    if (len) {
        if (len > std::numeric_limits<off_t>::max())
            throw Error("cannot preallocate contents for a file because it's too large");

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
    createUnknownSymlinkAt(
        parentDir,
        OsCanonPath{name},
#ifdef _WIN32
        string_to_os_string(target)
#else
        /* `OsString` is `std::string`, so no need to copy. */
        target
#endif
    );
    if (hooks)
        hooks->symlinkCreated(parentDir, name);
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
