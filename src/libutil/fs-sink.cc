#include <fcntl.h>

#include "error.hh"
#include "config-global.hh"
#include "fs-sink.hh"

#if _WIN32
# include <fileapi.h>
# include "file-path.hh"
# include "windows-error.hh"
#endif

namespace nix {

void copyRecursive(
    SourceAccessor & accessor, const CanonPath & from,
    FileSystemObjectSink & sink, const CanonPath & to)
{
    auto stat = accessor.lstat(from);

    switch (stat.type) {
    case SourceAccessor::tSymlink:
    {
        sink.createSymlink(to, accessor.readLink(from));
        break;
    }

    case SourceAccessor::tRegular:
    {
        sink.createRegularFile(to, [&](CreateRegularFileSink & crf) {
            if (stat.isExecutable)
                crf.isExecutable();
            accessor.readFile(from, crf, [&](uint64_t size) {
                crf.preallocateContents(size);
            });
        });
        break;
    }

    case SourceAccessor::tDirectory:
    {
        sink.createDirectory(to);
        for (auto & [name, _] : accessor.readDirectory(from)) {
            copyRecursive(
                accessor, from / name,
                sink, to / name);
            break;
        }
        break;
    }

    case SourceAccessor::tMisc:
        throw Error("file '%1%' has an unsupported type", from);

    default:
        abort();
    }
}


struct RestoreSinkSettings : Config
{
    Setting<bool> preallocateContents{this, false, "preallocate-contents",
        "Whether to preallocate files when writing objects with known size."};
};

static RestoreSinkSettings restoreSinkSettings;

static GlobalConfig::Register r1(&restoreSinkSettings);


void RestoreSink::createDirectory(const CanonPath & path)
{
    std::filesystem::create_directory(dstPath / path.rel());
};

struct RestoreRegularFile : CreateRegularFileSink {
    AutoCloseFD fd;

    void operator () (std::string_view data) override;
    void isExecutable() override;
    void preallocateContents(uint64_t size) override;
};

static std::filesystem::path append(const std::filesystem::path & src, const CanonPath & path)
{
    auto dst = src;
    if (!path.rel().empty())
        dst /= path.rel();
    return dst;
}

void RestoreSink::createRegularFile(const CanonPath & path, std::function<void(CreateRegularFileSink &)> func)
{
    auto p = append(dstPath, path);

    RestoreRegularFile crf;
    crf.fd =
#ifdef _WIN32
        CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)
#else
        open(p.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0666)
#endif
        ;
    if (!crf.fd) throw NativeSysError("creating file '%1%'", p);
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

void RestoreRegularFile::operator () (std::string_view data)
{
    writeFull(fd.get(), data);
}

void RestoreSink::createSymlink(const CanonPath & path, const std::string & target)
{
    auto p = append(dstPath, path);
    nix::createSymlink(target, p);
}


void RegularFileSink::createRegularFile(const CanonPath & path, std::function<void(CreateRegularFileSink &)> func)
{
    struct CRF : CreateRegularFileSink {
        RegularFileSink & back;
        CRF(RegularFileSink & back) : back(back) {}
        void operator () (std::string_view data) override
        {
            back.sink(data);
        }
        void isExecutable() override {}
    } crf { *this };
    func(crf);
}


void NullFileSystemObjectSink::createRegularFile(const CanonPath & path, std::function<void(CreateRegularFileSink &)> func)
{
    struct : CreateRegularFileSink {
        void operator () (std::string_view data) override {}
        void isExecutable() override {}
    } crf;
    // Even though `NullFileSystemObjectSink` doesn't do anything, it's important
    // that we call the function, to e.g. advance the parser using this
    // sink.
    func(crf);
}

}
