#include "posix-source-accessor.hh"

namespace nix {

void PosixSourceAccessor::readFile(
    const CanonPath & path,
    Sink & sink,
    std::function<void(uint64_t)> sizeCallback)
{
    // FIXME: add O_NOFOLLOW since symlinks should be resolved by the
    // caller?
    AutoCloseFD fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (!fd)
        throw SysError("opening file '%1%'", path);

    struct stat st;
    if (fstat(fd.get(), &st) == -1)
        throw SysError("statting file");

    sizeCallback(st.st_size);

    off_t left = st.st_size;

    std::vector<unsigned char> buf(64 * 1024);
    while (left) {
        checkInterrupt();
        ssize_t rd = read(fd.get(), buf.data(), (size_t) std::min(left, (off_t) buf.size()));
        if (rd == -1) {
            if (errno != EINTR)
                throw SysError("reading from file '%s'", showPath(path));
        }
        else if (rd == 0)
            throw SysError("unexpected end-of-file reading '%s'", showPath(path));
        else {
            assert(rd <= left);
            sink({(char *) buf.data(), (size_t) rd});
            left -= rd;
        }
    }
}

bool PosixSourceAccessor::pathExists(const CanonPath & path)
{
    return nix::pathExists(path.abs());
}

SourceAccessor::Stat PosixSourceAccessor::lstat(const CanonPath & path)
{
    auto st = nix::lstat(path.abs());
    mtime = std::max(mtime, st.st_mtime);
    return Stat {
        .type =
            S_ISREG(st.st_mode) ? tRegular :
            S_ISDIR(st.st_mode) ? tDirectory :
            S_ISLNK(st.st_mode) ? tSymlink :
            tMisc,
        .isExecutable = S_ISREG(st.st_mode) && st.st_mode & S_IXUSR
    };
}

SourceAccessor::DirEntries PosixSourceAccessor::readDirectory(const CanonPath & path)
{
    DirEntries res;
    for (auto & entry : nix::readDirectory(path.abs())) {
        std::optional<Type> type;
        switch (entry.type) {
        case DT_REG: type = Type::tRegular; break;
        case DT_LNK: type = Type::tSymlink; break;
        case DT_DIR: type = Type::tDirectory; break;
        }
        res.emplace(entry.name, type);
    }
    return res;
}

std::string PosixSourceAccessor::readLink(const CanonPath & path)
{
    return nix::readLink(path.abs());
}

std::optional<CanonPath> PosixSourceAccessor::getPhysicalPath(const CanonPath & path)
{
    return path;
}

}
