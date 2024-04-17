#include "posix-source-accessor.hh"
#include "signals.hh"
#include "sync.hh"

#include <unordered_map>

namespace nix {

PosixSourceAccessor::PosixSourceAccessor(std::filesystem::path && root)
    : root(std::move(root))
{
    assert(root.empty() || root.is_absolute());
    displayPrefix = root.string();
}

PosixSourceAccessor::PosixSourceAccessor()
    : PosixSourceAccessor(std::filesystem::path {})
{ }

std::pair<PosixSourceAccessor, CanonPath> PosixSourceAccessor::createAtRoot(const std::filesystem::path & path)
{
    std::filesystem::path path2 = absPath(path.string());
    return {
        PosixSourceAccessor { path2.root_path() },
        CanonPath { path2.relative_path().string() },
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

    AutoCloseFD fd = toDescriptor(open(ap.string().c_str(), O_RDONLY
    #ifndef _WIN32
        | O_NOFOLLOW | O_CLOEXEC
    #endif
        ));
    if (!fd)
        throw SysError("opening file '%1%'", ap.string());

    struct stat st;
    if (fstat(fromDescriptorReadOnly(fd.get()), &st) == -1)
        throw SysError("statting file");

    sizeCallback(st.st_size);

    off_t left = st.st_size;

    std::array<unsigned char, 64 * 1024> buf;
    while (left) {
        checkInterrupt();
        ssize_t rd = read(fromDescriptorReadOnly(fd.get()), buf.data(), (size_t) std::min(left, (off_t) buf.size()));
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
    return nix::pathExists(makeAbsPath(path).string());
}

std::optional<struct stat> PosixSourceAccessor::cachedLstat(const CanonPath & path)
{
    static Sync<std::unordered_map<Path, std::optional<struct stat>>> _cache;

    // Note: we convert std::filesystem::path to Path because the
    // former is not hashable on libc++.
    Path absPath = makeAbsPath(path).string();

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
    for (auto & entry : nix::readDirectory(makeAbsPath(path).string())) {
        std::optional<Type> type;
        switch (entry.type) {
        case DT_REG: type = Type::tRegular; break;
    #ifndef _WIN32
        case DT_LNK: type = Type::tSymlink; break;
    #endif
        case DT_DIR: type = Type::tDirectory; break;
        }
        res.emplace(entry.name, type);
    }
    return res;
}

std::string PosixSourceAccessor::readLink(const CanonPath & path)
{
    if (auto parent = path.parent()) assertNoSymlinks(*parent);
    return nix::readLink(makeAbsPath(path).string());
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
