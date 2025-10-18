#include <fcntl.h>

#include "nix/util/error.hh"
#include "nix/util/config-global.hh"
#include "nix/util/fs-sink.hh"

#ifdef _WIN32
#  include <fileapi.h>
#  include "nix/util/file-path.hh"
#  include "nix/util/windows-error.hh"
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
        sink.createDirectory(to);
        for (auto & [name, _] : accessor.readDirectory(from)) {
            copyRecursive(accessor, from / name, sink, to / name);
            break;
        }
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

void RestoreSink::createDirectory(const CanonPath & path)
{
    auto p = append(dstPath, path);
    if (!std::filesystem::create_directory(p))
        throw Error("path '%s' already exists", p.string());
};

struct RestoreRegularFile : CreateRegularFileSink
{
    AutoCloseFD fd;
    bool startFsync = false;

    ~RestoreRegularFile()
    {
        /* Initiate an fsync operation without waiting for the
           result. The real fsync should be run before registering a
           store path, but this is a performance optimization to allow
           the disk write to start early. */
        if (fd && startFsync)
            fd.startFsync();
    }

    void operator()(std::string_view data) override;
    void isExecutable() override;
    void preallocateContents(uint64_t size) override;
};

void RestoreSink::createRegularFile(const CanonPath & path, std::function<void(CreateRegularFileSink &)> func)
{
    auto p = append(dstPath, path);

    RestoreRegularFile crf;
    crf.startFsync = startFsync;
    crf.fd =
#ifdef _WIN32
        CreateFileW(
            p.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            NULL)
#else
        open(p.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0666)
#endif
        ;
    if (!crf.fd)
        throw NativeSysError("creating file '%1%'", p);
    func(crf);
}

void RestoreRegularFile::isExecutable()
{
    // Windows doesn't have a notion of executable file permissions we
    // care about here, right?
#ifndef _WIN32
    struct stat st;
    if (fstat(fd.get(), &st) == -1)
        throw SysError("fstat");
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

void RestoreRegularFile::operator()(std::string_view data)
{
    writeFull(fd.get(), data);
}

void RestoreSink::createSymlink(const CanonPath & path, const std::string & target)
{
    auto p = append(dstPath, path);
    nix::createSymlink(target, p.string());
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
