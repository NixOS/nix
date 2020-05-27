#pragma once

#include "types.hh"
#include "serialise.hh"
#include "archive.hh"

namespace nix {

struct RestoreSink : ParseSink
{
    Path dstPath;
    AutoCloseFD fd;

    void createDirectory(const Path & path)
    {
        Path p = dstPath + path;
        if (mkdir(p.c_str(), 0777) == -1)
            throw SysError(format("creating directory '%1%'") % p);
    };

    void createRegularFile(const Path & path)
    {
        Path p = dstPath + path;
        fd = open(p.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0666);
        if (!fd) throw SysError(format("creating file '%1%'") % p);
    }

    void isExecutable()
    {
        struct stat st;
        if (fstat(fd.get(), &st) == -1)
            throw SysError("fstat");
        if (fchmod(fd.get(), st.st_mode | (S_IXUSR | S_IXGRP | S_IXOTH)) == -1)
            throw SysError("fchmod");
    }

    void preallocateContents(unsigned long long len)
    {
#if HAVE_POSIX_FALLOCATE
        if (len) {
            errno = posix_fallocate(fd.get(), 0, len);
            /* Note that EINVAL may indicate that the underlying
               filesystem doesn't support preallocation (e.g. on
               OpenSolaris).  Since preallocation is just an
               optimisation, ignore it. */
            if (errno && errno != EINVAL && errno != EOPNOTSUPP && errno != ENOSYS)
                throw SysError(format("preallocating file of %1% bytes") % len);
        }
#endif
    }

    void receiveContents(unsigned char * data, unsigned int len)
    {
        writeFull(fd.get(), data, len);
    }

    void createSymlink(const Path & path, const string & target)
    {
        Path p = dstPath + path;
        nix::createSymlink(target, p);
    }
};


}
