#include <fcntl.h>

#include "config.hh"
#include "fs-sink.hh"

namespace nix {


struct RestoreSinkSettings : Config
{
    Setting<bool> preallocateContents{this, true, "preallocate-contents",
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

void RestoreSink::createExecutableFile(const Path & path)
{
    Path p = dstPath + path;
    fd = open(p.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0777);
    if (!fd) throw SysError("creating file '%1%'", p);
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

#ifdef HAVE_POSIX_FALLOCATE
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

void RestoreSink::receiveContents(unsigned char * data, size_t len)
{
    writeFull(fd.get(), data, len);
}

void RestoreSink::createSymlink(const Path & path, const string & target)
{
    Path p = dstPath + path;
    nix::createSymlink(target, p);
}

void RestoreSink::copyFile(const Path & source)
{
    FdSink sink(fd.get());
    readFile(source, sink);
}

void RestoreSink::copyDirectory(const Path & source, const Path & destination)
{
    Path p = dstPath + destination;
    createDirectory(destination);
    for (auto & i : readDirectory(source)) {
        struct stat st;
        Path entry = source + "/" + i.name;
        if (lstat(entry.c_str(), &st))
            throw SysError("getting attributes of path '%1%'", entry);
        if (S_ISREG(st.st_mode)) {
            if (st.st_mode & S_IXUSR)
                createExecutableFile(destination + "/" + i.name);
            else
                createRegularFile(destination + "/" + i.name);
            copyFile(entry);
        } else if (S_ISDIR(st.st_mode))
            copyDirectory(entry, destination + "/" + i.name);
        else
            throw Error("Unknown file: %s", entry);
    }
}


}
