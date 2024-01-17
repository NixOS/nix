#include "posix-source-accessor.hh"
#include "signals.hh"
#include "sync.hh"

#include <unordered_map>

namespace nix {

void PosixSourceAccessor::readFile(
    const CanonPath & path,
    Sink & sink,
    std::function<void(uint64_t)> sizeCallback)
{
    assertNoSymlinks(path);

    AutoCloseFD fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (!fd)
        throw SysError("opening file '%1%'", path);

    struct stat st;
    if (fstat(fd.get(), &st) == -1)
        throw SysError("statting file");

    sizeCallback(st.st_size);

    off_t left = st.st_size;

    std::array<unsigned char, 64 * 1024> buf;
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
    if (auto parent = path.parent()) assertNoSymlinks(*parent);
    return nix::pathExists(path.abs());
}

std::optional<struct stat> PosixSourceAccessor::cachedLstat(const CanonPath & path)
{
    static Sync<std::unordered_map<CanonPath, std::optional<struct stat>>> _cache;

    {
        auto cache(_cache.lock());
        auto i = cache->find(path);
        if (i != cache->end()) return i->second;
    }

    std::optional<struct stat> st{std::in_place};
    if (::lstat(path.c_str(), &*st)) {
        if (errno == ENOENT || errno == ENOTDIR)
            st.reset();
        else
            throw SysError("getting status of '%s'", showPath(path));
    }

    auto cache(_cache.lock());
    if (cache->size() >= 16384) cache->clear();
    cache->emplace(path, st);

    return st;
}

std::optional<SourceAccessor::Stat> PosixSourceAccessor::maybeLstat(const CanonPath & path)
{
    if (auto parent = path.parent()) assertNoSymlinks(*parent);
    auto st = cachedLstat(path);
    if (!st) return std::nullopt;
    mtime = std::max(mtime, st->st_mtime);
    return Stat {
        .type =
            S_ISREG(st->st_mode) ? tRegular :
            S_ISDIR(st->st_mode) ? tDirectory :
            S_ISLNK(st->st_mode) ? tSymlink :
            tMisc,
        .fileSize = S_ISREG(st->st_mode) ? std::optional<uint64_t>(st->st_size) : std::nullopt,
        .isExecutable = S_ISREG(st->st_mode) && st->st_mode & S_IXUSR,
    };
}

SourceAccessor::DirEntries PosixSourceAccessor::readDirectory(const CanonPath & path)
{
    assertNoSymlinks(path);
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
    if (auto parent = path.parent()) assertNoSymlinks(*parent);
    return nix::readLink(path.abs());
}

std::optional<CanonPath> PosixSourceAccessor::getPhysicalPath(const CanonPath & path)
{
    return path;
}

void PosixSourceAccessor::assertNoSymlinks(CanonPath path)
{
    while (!path.isRoot()) {
        auto st = cachedLstat(path);
        if (st && S_ISLNK(st->st_mode))
            throw Error("path '%s' is a symlink", showPath(path));
        path.pop();
    }
}

}
