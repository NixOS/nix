#include "source-accessor.hh"
#include "archive.hh"

namespace nix {

static std::atomic<size_t> nextNumber{0};

SourceAccessor::SourceAccessor()
    : number(++nextNumber)
{
}

Hash SourceAccessor::hashPath(
    const CanonPath & path,
    PathFilter & filter,
    HashType ht)
{
    HashSink sink(ht);
    dumpPath(path, sink, filter);
    return sink.finish().first;
}

std::optional<SourceAccessor::Stat> SourceAccessor::maybeLstat(const CanonPath & path)
{
    // FIXME: merge these into one operation.
    if (!pathExists(path))
        return {};
    return lstat(path);
}

std::string SourceAccessor::showPath(const CanonPath & path)
{
    return path.abs();
}

std::string PosixSourceAccessor::readFile(const CanonPath & path)
{
    return nix::readFile(path.abs());
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
