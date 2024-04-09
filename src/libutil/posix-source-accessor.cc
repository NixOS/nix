#include "posix-source-accessor.hh"
#include "signals.hh"
#include "sync.hh"

#include <unordered_map>

namespace nix {

PosixSourceAccessor::PosixSourceAccessor(std::filesystem::path && root)
    : root(std::move(root))
{
    assert(root.empty() || root.is_absolute());
    displayPrefix = root;
}

PosixSourceAccessor::PosixSourceAccessor()
    : PosixSourceAccessor(std::filesystem::path {})
{ }

std::pair<PosixSourceAccessor, CanonPath> PosixSourceAccessor::createAtRoot(const std::filesystem::path & path)
{
    std::filesystem::path path2 = absPath(path.native());
    return {
        PosixSourceAccessor { path2.root_path() },
        CanonPath { static_cast<std::string>(path2.relative_path()) },
    };
}

std::filesystem::path PosixSourceAccessor::makeAbsPath(const CanonPath & path)
{
    return root.empty()
        ? (std::filesystem::path { path.abs() })
        : path.isRoot()
        ? /* Don't append a slash for the root of the accessor, since
             it can be a non-directory (e.g. in the case of `fetchTree
             { type = "file" }`). */
          root
        : root / path.rel();
}

void PosixSourceAccessor::readFile(
    const CanonPath & path,
    Sink & sink,
    std::function<void(uint64_t)> sizeCallback)
{
    assertNoSymlinks(path);

    auto ap = makeAbsPath(path);

    AutoCloseFD fd = open(ap.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (!fd)
        throw SysError("opening file '%1%'", ap.native());

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
    return nix::pathExists(makeAbsPath(path));
}

std::optional<struct stat> PosixSourceAccessor::cachedLstat(const CanonPath & path)
{
    static Sync<std::unordered_map<Path, std::optional<struct stat>>> _cache;

    // Note: we convert std::filesystem::path to Path because the
    // former is not hashable on libc++.
    Path absPath = makeAbsPath(path);

    {
        auto cache(_cache.lock());
        auto i = cache->find(absPath);
        if (i != cache->end()) return i->second;
    }

    auto st = nix::maybeLstat(absPath.c_str());

    auto cache(_cache.lock());
    if (cache->size() >= 16384) cache->clear();
    cache->emplace(absPath, st);

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
    for (auto & entry : nix::readDirectory(makeAbsPath(path))) {
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
    return nix::readLink(makeAbsPath(path));
}

std::optional<std::filesystem::path> PosixSourceAccessor::getPhysicalPath(const CanonPath & path)
{
    return makeAbsPath(path);
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
