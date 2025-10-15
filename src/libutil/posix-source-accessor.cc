#include "nix/util/posix-source-accessor.hh"
#include "nix/util/source-path.hh"
#include "nix/util/signals.hh"
#include "nix/util/sync.hh"

#include <boost/unordered/concurrent_flat_map.hpp>

namespace nix {

PosixSourceAccessor::PosixSourceAccessor(std::filesystem::path && argRoot)
    : root(std::move(argRoot))
{
    assert(root.empty() || root.is_absolute());
    displayPrefix = root.string();
}

PosixSourceAccessor::PosixSourceAccessor()
    : PosixSourceAccessor(std::filesystem::path{})
{
}

SourcePath PosixSourceAccessor::createAtRoot(const std::filesystem::path & path)
{
    std::filesystem::path path2 = absPath(path);
    return {
        make_ref<PosixSourceAccessor>(path2.root_path()),
        CanonPath{path2.relative_path().string()},
    };
}

std::filesystem::path PosixSourceAccessor::makeAbsPath(const CanonPath & path)
{
    return root.empty()    ? (std::filesystem::path{path.abs()})
           : path.isRoot() ? /* Don't append a slash for the root of the accessor, since
                                it can be a non-directory (e.g. in the case of `fetchTree
                                { type = "file" }`). */
               root
                           : root / path.rel();
}

void PosixSourceAccessor::readFile(const CanonPath & path, Sink & sink, std::function<void(uint64_t)> sizeCallback)
{
    assertNoSymlinks(path);

    auto ap = makeAbsPath(path);

    AutoCloseFD fd = toDescriptor(open(
        ap.string().c_str(),
        O_RDONLY
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
        } else if (rd == 0)
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
    if (auto parent = path.parent())
        assertNoSymlinks(*parent);
    return nix::pathExists(makeAbsPath(path).string());
}

std::optional<struct stat> PosixSourceAccessor::cachedLstat(const CanonPath & path)
{
    using Cache = boost::concurrent_flat_map<Path, std::optional<struct stat>>;
    static Cache cache;

    // Note: we convert std::filesystem::path to Path because the
    // former is not hashable on libc++.
    Path absPath = makeAbsPath(path).string();

    if (auto res = getConcurrent(cache, absPath))
        return *res;

    auto st = nix::maybeLstat(absPath.c_str());

    if (cache.size() >= 16384)
        cache.clear();
    cache.emplace(std::move(absPath), st);

    return st;
}

std::optional<SourceAccessor::Stat> PosixSourceAccessor::maybeLstat(const CanonPath & path)
{
    if (auto parent = path.parent())
        assertNoSymlinks(*parent);
    auto st = cachedLstat(path);
    if (!st)
        return std::nullopt;
    // This makes the accessor thread-unsafe, but we only seem to use the actual value in a single threaded context in
    // `src/libfetchers/path.cc`.
    mtime = std::max(mtime, st->st_mtime);
    return Stat{
        .type = S_ISREG(st->st_mode)   ? tRegular
                : S_ISDIR(st->st_mode) ? tDirectory
                : S_ISLNK(st->st_mode) ? tSymlink
                : S_ISCHR(st->st_mode) ? tChar
                : S_ISBLK(st->st_mode) ? tBlock
                :
#ifdef S_ISSOCK
                S_ISSOCK(st->st_mode) ? tSocket
                :
#endif
                S_ISFIFO(st->st_mode) ? tFifo
                                      : tUnknown,
        .fileSize = S_ISREG(st->st_mode) ? std::optional<uint64_t>(st->st_size) : std::nullopt,
        .isExecutable = S_ISREG(st->st_mode) && st->st_mode & S_IXUSR,
    };
}

SourceAccessor::DirEntries PosixSourceAccessor::readDirectory(const CanonPath & path)
{
    assertNoSymlinks(path);
    DirEntries res;
    for (auto & entry : DirectoryIterator{makeAbsPath(path)}) {
        checkInterrupt();
        auto type = [&]() -> std::optional<Type> {
            try {
                /* WARNING: We are specifically not calling symlink_status()
                 * here, because that always translates to `stat` call and
                 * doesn't make use of any caching. Instead, we have to
                 * rely on the myriad of `is_*` functions, which actually do
                 * the caching. If you are in doubt then take a look at the
                 * libstdc++ implementation [1] and the standard proposal
                 * about the caching variations of directory_entry [2].

                 * [1]:
                 https://github.com/gcc-mirror/gcc/blob/8ea555b7b4725dbc5d9286f729166cd54ce5b615/libstdc%2B%2B-v3/include/bits/fs_dir.h#L341-L348
                 * [2]: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0317r1.html
                 */

                /* Check for symlink first, because other getters follow symlinks. */
                if (entry.is_symlink())
                    return tSymlink;
                if (entry.is_regular_file())
                    return tRegular;
                if (entry.is_directory())
                    return tDirectory;
                if (entry.is_character_file())
                    return tChar;
                if (entry.is_block_file())
                    return tBlock;
                if (entry.is_fifo())
                    return tFifo;
                if (entry.is_socket())
                    return tSocket;
                return tUnknown;
            } catch (std::filesystem::filesystem_error & e) {
                // We cannot always stat the child. (Ideally there is no
                // stat because the native directory entry has the type
                // already, but this isn't always the case.)
                if (e.code() == std::errc::permission_denied || e.code() == std::errc::operation_not_permitted)
                    return std::nullopt;
                else
                    throw;
            }
        }();
        res.emplace(entry.path().filename().string(), type);
    }
    return res;
}

std::string PosixSourceAccessor::readLink(const CanonPath & path)
{
    if (auto parent = path.parent())
        assertNoSymlinks(*parent);
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

ref<SourceAccessor> getFSSourceAccessor()
{
    static auto rootFS = make_ref<PosixSourceAccessor>();
    return rootFS;
}

ref<SourceAccessor> makeFSSourceAccessor(std::filesystem::path root)
{
    return make_ref<PosixSourceAccessor>(std::move(root));
}
} // namespace nix
