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


void RestoreSink::createDirectory(const Path & path)
{
    Path p = dstPath + path;
    if (mkdir(p.c_str(), 0777) == -1)
        throw SysError("creating directory '%1%'", p);
};

void RestoreSink::createRegularFile(const Path & path)
{
    Path p = dstPath + path;
    fd = open(p.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0666);
    if (!fd) throw SysError("creating file '%1%'", p);
}

void RestoreSink::closeRegularFile()
{
    /* Call close explicitly to make sure the error is checked */
    fd.close();
}

void RestoreSink::isExecutable()
{
    struct stat st;
    if (fstat(fd.get(), &st) == -1)
        throw SysError("fstat");
    if (fchmod(fd.get(), st.st_mode | (S_IXUSR | S_IXGRP | S_IXOTH)) == -1)
        throw SysError("fchmod");
}

void RestoreSink::preallocateContents(uint64_t len)
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

void RestoreSink::receiveContents(std::string_view data)
{
    writeFull(fd.get(), data);
}

void RestoreSink::createSymlink(const Path & path, const std::string & target)
{
    Path p = dstPath + path;
    nix::createSymlink(target, p);
}

}
