#include "nix/util/posix-source-accessor.hh"
#include "nix/util/file-system-at.hh"
#include "nix/util/memory-source-accessor.hh"
#include "nix/util/source-path.hh"
#include "nix/util/signals.hh"
#include "nix/util/sync.hh"

#include <boost/unordered/concurrent_flat_map.hpp>

namespace nix {

static SourceAccessor::Stat sourceAccessorStatFromPosixStat(const PosixStat & st)
{
    using enum SourceAccessor::Type;
    return SourceAccessor::Stat{
        .type = S_ISREG(st.st_mode)   ? tRegular
                : S_ISDIR(st.st_mode) ? tDirectory
                : S_ISLNK(st.st_mode) ? tSymlink
                : S_ISCHR(st.st_mode) ? tChar
                : S_ISBLK(st.st_mode) ? tBlock
                :
#ifdef S_ISSOCK
                S_ISSOCK(st.st_mode) ? tSocket
                :
#endif
                S_ISFIFO(st.st_mode) ? tFifo
                                     : tUnknown,
        .fileSize = S_ISREG(st.st_mode) ? std::optional<uint64_t>(st.st_size) : std::nullopt,
        .isExecutable = S_ISREG(st.st_mode) && st.st_mode & S_IXUSR,
    };
}

namespace {

class PosixFileSourceAccessor : public detail::PosixSourceAccessorBase
{
    AutoCloseFD fd;
    std::filesystem::path fsPath;
    /**
     * Stat is memoised once opened. This does mean that modifying the same file
     * while we are reading is busted, but caching it might be considered an
     * improvement. Since we have a file descriptor for a regular file it can't
     * be swapped out for another file type. Thus, we are only really caching
     * the file size and mtime, which shouldn't change. Providing a consistent
     * size value here is also fine. If the file becomes smaller than we expect
     * then readFile will barf with an EOF exception. If it becomes larger then
     * we are just going to silently ignore the extra bytes.
     */
    PosixStat st;

public:
    PosixFileSourceAccessor(AutoCloseFD fd, std::filesystem::path path, bool trackLastModified, const PosixStat & st_)
        : PosixSourceAccessorBase(trackLastModified)
        , fd(std::move(fd))
        , fsPath(std::move(path))
        , st(st_)
    {
        assert(S_ISREG(st.st_mode));
        assert(fsPath.is_absolute()); /* Only used for error messages, but still nice to enforce this invariant. */
        setPathDisplay(fsPath.generic_string());
        maybeUpdateMtime(st.st_mtime);
    }

    void readFile(const CanonPath & path, Sink & sink, fun<void(uint64_t)> sizeCallback) override;

    bool pathExists(const CanonPath & path) override;

    std::optional<Stat> maybeLstat(const CanonPath & path) override;

    DirEntries readDirectory(const CanonPath & path) override;

    std::string readLink(const CanonPath & path) override;

    std::optional<std::filesystem::path> getPhysicalPath(const CanonPath & path) override
    {
        if (path.isRoot())
            return fsPath;
        return std::nullopt; /* Definitely doesn't exist. */
    }

    std::string showPath(const CanonPath & path) override
    {
        if (path.isRoot())
            return displayPrefix; /* No trailing slash. */
        return displayPrefix + path.abs();
    }
};

void PosixFileSourceAccessor::readFile(const CanonPath & path, Sink & sink, fun<void(uint64_t)> sizeCallback)
{
    if (!path.isRoot()) /* We are the parent and also a regular file. */
        throw NotADirectory("reading file '%1%': %2%", showPath(path), "Not a directory");

    auto size = st.st_size;
    sizeCallback(size);
    /* The most important invariant we care about here is writing exactly size
       bytes to the sink. copyFdRange should throw an EndOfFile if we fail to read
       `size` bytes. */
    copyFdRange(fd.get(), /*offset=*/0, size, sink);
}

bool PosixFileSourceAccessor::pathExists(const CanonPath & path)
{
    return path.isRoot();
}

std::optional<SourceAccessor::Stat> PosixFileSourceAccessor::maybeLstat(const CanonPath & path)
{
    if (!path.isRoot())
        return std::nullopt;
    return sourceAccessorStatFromPosixStat(st);
}

SourceAccessor::DirEntries PosixFileSourceAccessor::readDirectory(const CanonPath & path)
{
    throw NotADirectory("reading directory '%1%': %2%", showPath(path), "Not a directory");
}

std::string PosixFileSourceAccessor::readLink(const CanonPath & path)
{
    if (!path.isRoot())
        throw NotADirectory("reading symlink '%1%': %2%", showPath(path), "Not a directory");
    throw NotASymlink("path '%1%' is not a symlink", showPath(path));
}

} // namespace

PosixSourceAccessor::PosixSourceAccessor(std::filesystem::path && argRoot, bool trackLastModified)
    : PosixSourceAccessorBase(trackLastModified)
    , root(std::move(argRoot))
{
    assert(root.empty() || root.is_absolute());
    displayPrefix = root.string();
}

PosixSourceAccessor::PosixSourceAccessor()
    : PosixSourceAccessor(std::filesystem::path{})
{
}

SourcePath PosixSourceAccessor::createAtRoot(const std::filesystem::path & path, bool trackLastModified)
{
    std::filesystem::path path2 = absPath(path);
    return {
        make_ref<PosixSourceAccessor>(path2.root_path(), trackLastModified),
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

void PosixSourceAccessor::readFile(const CanonPath & path, Sink & sink, fun<void(uint64_t)> sizeCallback)
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

    auto size = getFileSize(fd.get());

    sizeCallback(size);
    FdSource source(fd.get());
    /* The most important invariant we care about here is writing exactly size
       bytes to the sink. drainInto should throw an EndOfFile if we fail to read
       `size` bytes. */
    source.drainInto(sink, size);
}

bool PosixSourceAccessor::pathExists(const CanonPath & path)
{
    if (auto parent = path.parent())
        assertNoSymlinks(*parent);
    return nix::pathExists(makeAbsPath(path).string());
}

using Cache = boost::concurrent_flat_map<std::string, std::optional<PosixStat>>;
static Cache cache;

std::optional<PosixStat> PosixSourceAccessor::cachedLstat(const CanonPath & path)
{
    // Note: we convert std::filesystem::path to std::string because the
    // former is not hashable on libc++.
    std::string absPath = makeAbsPath(path).string();

    if (auto res = getConcurrent(cache, absPath))
        return *res;

    auto st = nix::maybeLstat(absPath.c_str());

    if (cache.size() >= 16384)
        cache.clear();
    cache.emplace(std::move(absPath), st);

    return st;
}

void PosixSourceAccessor::invalidateCache(const CanonPath & path)
{
    cache.erase(makeAbsPath(path).string());
}

std::optional<SourceAccessor::Stat> PosixSourceAccessor::maybeLstat(const CanonPath & path)
{
    if (auto parent = path.parent())
        assertNoSymlinks(*parent);
    auto st = cachedLstat(path);
    if (!st)
        return std::nullopt;

    PosixSourceAccessorBase::maybeUpdateMtime(st->st_mtime);
    return sourceAccessorStatFromPosixStat(*st);
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
                    throw SystemError(e.code(), "getting status of '%s'", PathFmt(entry.path()));
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
    return nix::readLink(makeAbsPath(path)).string();
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
            throw SymlinkNotAllowed(path, "path '%s' is a symlink", showPath(path));
        path.pop();
    }
}

ref<SourceAccessor> getFSSourceAccessor()
{
    static auto rootFS = make_ref<PosixSourceAccessor>();
    return rootFS;
}

ref<SourceAccessor> makeFSSourceAccessor(std::filesystem::path root, bool trackLastModified)
{
#ifndef _WIN32
    assert(root.is_absolute());
    AutoCloseFD fd = openFileReadonly(root, FinalSymlink::DontFollow);

    if (!fd) {
        if (errno == ELOOP) {
            /* This branch is taken either if the final component is a symlink
               or we hit a symlink loop. If that's the latter this will also
               throw. This can be done with O_PATH descriptor for the symlink
               itself, but it's not portable. */
            auto linkTarget = readLink(root);
            auto res = make_ref<MemorySourceAccessor>();
            /* Create an in-memory accessor with the symlink at the root. */
            MemorySink sink{*res};
            sink.createSymlink(CanonPath::root, os_string_to_string(linkTarget));
            return res;
        }

        throw NativeSysError("opening file %1%", PathFmt(root));
    }

    auto st = nix::fstat(fd.get());
    if (S_ISREG(st.st_mode))
        return make_ref<PosixFileSourceAccessor>(std::move(fd), std::move(root), trackLastModified, st);

    /* TODO: Use the file descriptor for fd-relative operations on the directory. */
#endif

    return make_ref<PosixSourceAccessor>(std::move(root), trackLastModified);
}

} // namespace nix
