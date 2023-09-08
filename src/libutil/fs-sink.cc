#include <fcntl.h>

#include "config.hh"
#include "fs-sink.hh"

namespace nix {

struct RestoreSinkSettings : Config
{
    Setting<bool> preallocateContents{this, false, "preallocate-contents",
        "Whether to preallocate files when writing objects with known size."};
};

static RestoreSinkSettings restoreSinkSettings;

static GlobalConfig::Register r1(&restoreSinkSettings);

struct RestoreSink : ParseSink
{
    Path dstPath;
    AutoCloseFD fd;

    void createDirectory(const Path & path) override
    {
        Path p = dstPath + path;
        if (mkdir(p.c_str(), 0777) == -1)
            throw SysError("creating directory '%1%'", p);
    };

    void createRegularFile(const Path & path) override
    {
        Path p = dstPath + path;
        fd = open(p.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0666);
        if (!fd) throw SysError("creating file '%1%'", p);
    }

    void closeRegularFile() override
    {
        /* Call close explicitly to make sure the error is checked */
        fd.close();
    }

    void isExecutable() override
    {
        struct stat st;
        if (fstat(fd.get(), &st) == -1)
            throw SysError("fstat");
        if (fchmod(fd.get(), st.st_mode | (S_IXUSR | S_IXGRP | S_IXOTH)) == -1)
            throw SysError("fchmod");
    }

    void preallocateContents(uint64_t len) override
    {
        if (!archiveSettings.preallocateContents)
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

    void receiveContents(std::string_view data) override
    {
        writeFull(fd.get(), data);
    }

    void createSymlink(const Path & path, const std::string & target) override
    {
        Path p = dstPath + path;
        nix::createSymlink(target, p);
    }
};

}
