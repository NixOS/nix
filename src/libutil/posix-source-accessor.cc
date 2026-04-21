#include "nix/util/posix-source-accessor.hh"
#include "nix/util/file-system-at.hh"
#include "nix/util/lru-cache.hh"
#include "nix/util/sync.hh"
#include "nix/util/memory-source-accessor.hh"
#include "nix/util/signals.hh"

#include <boost/unordered/concurrent_flat_map.hpp>

#include <atomic>

#ifndef _WIN32
#  include <sys/resource.h>
#endif

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

#ifndef _WIN32

class PosixDirectorySourceAccessor;

class PosixFileSourceAccessor : public detail::PosixSourceAccessorBase
{
    friend class PosixDirectorySourceAccessor;

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

static unsigned getGlobalDirFdCacheLimit()
{
    ::rlimit lim{};
    if (::getrlimit(RLIMIT_NOFILE, &lim) == -1)
        throw SysError("querying RLIMIT_NOFILE");
    /* Some sane upper bound in case we have a huge rlimit. */
    return std::min<rlim_t>(4096, lim.rlim_cur / 8);
}

class PosixDirectorySourceAccessor : public detail::PosixSourceAccessorBase
{
public:
    static unsigned getGlobalFdLimit()
    {
        static auto res = getGlobalDirFdCacheLimit();
        return res;
    }

    static void registerAccessor(ref<PosixDirectorySourceAccessor> accessor)
    {
        auto reg = globalDirFdCacheRegistry.lock();
        std::erase_if(*reg, [](auto & maybeAccessor) { return maybeAccessor.expired(); });
        reg->push_back(accessor.get_ptr());
    }

private:
    AutoCloseFD dirFd;
    std::filesystem::path fsPath;

    std::shared_ptr<Sync<LRUCache<CanonPath, ref<AutoCloseFD>>>> dirFdCache;

    static inline std::atomic<unsigned> globalDirFdCount = 0;

    static inline Sync<std::list<std::weak_ptr<PosixDirectorySourceAccessor>>> globalDirFdCacheRegistry;

    static void maybeEvictFromGlobalCaches()
    {
        if (globalDirFdCount.load(std::memory_order_relaxed) < getGlobalFdLimit())
            return;

        auto registry(globalDirFdCacheRegistry.lock());
        for (auto it = registry->begin(); it != registry->end();) {
            if (globalDirFdCount.load(std::memory_order_relaxed) < getGlobalFdLimit())
                break;

            auto accessor = it->lock();
            if (!accessor) {
                it = registry->erase(it);
                continue;
            }

            /* TODO: Would be nicer if we could evict a portion of the utilised
               cache to avoid cold-start issues. Should be fine for now. */
            if (accessor->dirFdCache) {
                auto cache = accessor->dirFdCache->lock();
                globalDirFdCount.fetch_sub(cache->size(), std::memory_order_relaxed);
                cache->clear();
            }

            ++it;
        }
    }

    void insertIntoDirFdCache(const CanonPath & key, ref<AutoCloseFD> fd)
    {
        assert(dirFdCache);
        auto cache = dirFdCache->lock();
        auto before = cache->size();
        cache->upsert(key, std::move(fd));
        globalDirFdCount.fetch_add(cache->size() - before, std::memory_order_relaxed);
    }

    /**
     * Get the parent directory of path. The second pair element might be an owning file descriptor
     * if path.parent().isRoot() is false.
     */
    std::pair<Descriptor, std::shared_ptr<AutoCloseFD>> openParent(const CanonPath & path);

    std::function<void(AutoCloseFD, CanonPath)> makeDirFdCallback();

    AutoCloseFD openSubdirectory(const CanonPath & path);

public:
    PosixDirectorySourceAccessor(
        AutoCloseFD fd, std::filesystem::path path, bool trackLastModified, unsigned dirFdCacheSize)
        : PosixSourceAccessorBase(trackLastModified)
        , dirFd(std::move(fd))
        , fsPath(std::move(path))
    {
        assert(fsPath.is_absolute()); /* Only used for error messages, but still nice to enforce this invariant. */
        setPathDisplay(fsPath.generic_string());

        if (dirFdCacheSize)
            dirFdCache = std::make_shared<Sync<LRUCache<CanonPath, ref<AutoCloseFD>>>>(dirFdCacheSize);
    }

    PosixDirectorySourceAccessor(PosixDirectorySourceAccessor &&) = delete;
    PosixDirectorySourceAccessor(const PosixDirectorySourceAccessor &) = delete;
    PosixDirectorySourceAccessor & operator=(PosixDirectorySourceAccessor &&) = delete;
    PosixDirectorySourceAccessor & operator=(const PosixDirectorySourceAccessor &) = delete;

    ~PosixDirectorySourceAccessor()
    {
        invalidateCache();
    }

    void invalidateCache() override
    {
        if (dirFdCache) {
            auto cache = dirFdCache->lock();
            globalDirFdCount.fetch_sub(cache->size(), std::memory_order_relaxed);
            cache->clear();
        }
    }

    void readFile(const CanonPath & path, Sink & sink, fun<void(uint64_t)> sizeCallback) override;

    std::optional<Stat> maybeLstat(const CanonPath & path) override;

    DirEntries readDirectory(const CanonPath & path) override;

    void readDirectory(
        const CanonPath & dirPath,
        std::function<void(SourceAccessor & subdirAccessor, const CanonPath & subdirRelPath)> callback) override;

    std::string readLink(const CanonPath & path) override;

    std::optional<std::filesystem::path> getPhysicalPath(const CanonPath & path) override
    {
        if (path.isRoot())
            return fsPath;
        return std::filesystem::path(fsPath) / path.rel(); /* RHS *must* be a relative path. */
    }

    std::string showPath(const CanonPath & path) override
    {
        if (path.isRoot())
            return displayPrefix; /* No trailing slash. */
        if (displayPrefix.ends_with('/'))
            return displayPrefix + path.rel();
        return displayPrefix + path.abs();
    }
};

std::function<void(AutoCloseFD, CanonPath)> PosixDirectorySourceAccessor::makeDirFdCallback()
{
    if (!dirFdCache)
        return nullptr;

    return [this](AutoCloseFD fd, CanonPath key) {
        assert(fd);
        insertIntoDirFdCache(std::move(key), make_ref<AutoCloseFD>(std::move(fd)));
    };
}

std::pair<Descriptor, std::shared_ptr<AutoCloseFD>> PosixDirectorySourceAccessor::openParent(const CanonPath & path)
{
    assert(!path.isRoot());
    auto parent = path.parent().value();
    if (parent.isRoot())
        return {dirFd.get(), nullptr};

    maybeEvictFromGlobalCaches();

    std::shared_ptr<AutoCloseFD> intermediateParentFd;
    CanonPath anchor = CanonPath::root;

    if (dirFdCache) {
        auto cache = dirFdCache->lock();
        auto p = parent;
        while (true) {
            if (auto intermediateDirFdHit = cache->get(p)) {
                if (p == parent)
                    return {(*intermediateDirFdHit)->get(), *intermediateDirFdHit};
                intermediateParentFd = intermediateDirFdHit->get_ptr();
                anchor = p;
                break;
            }
            if (p.isRoot())
                break;
            p.pop();
        }
    }

    Descriptor startFd = intermediateParentFd ? intermediateParentFd->get() : dirFd.get();
    CanonPath relPath = intermediateParentFd ? parent.removePrefix(anchor) : parent;

    std::function<void(AutoCloseFD, CanonPath)> cb;
    if (auto base = makeDirFdCallback()) {
        if (intermediateParentFd) {
            cb = [base = std::move(base), prefix = anchor](AutoCloseFD fd, CanonPath relKey) {
                base(std::move(fd), prefix / relKey);
            };
        } else {
            cb = std::move(base);
        }
    }

    try {
        AutoCloseFD parentFdOwning =
            openFileEnsureBeneathNoSymlinks(startFd, relPath, O_DIRECTORY | O_RDONLY | O_CLOEXEC, 0, std::move(cb));
        return {parentFdOwning.get(), make_ref<AutoCloseFD>(std::move(parentFdOwning))};
    } catch (SymlinkNotAllowed & e) {
        /* Need to fixup the error message to include the actual path relative to the (possibly) cached fd. */
        throw SymlinkNotAllowed(anchor / e.path, "path '%s' is a symlink", showPath(anchor / e.path));
    }
}

std::optional<SourceAccessor::Stat> PosixDirectorySourceAccessor::maybeLstat(const CanonPath & path)
try {
    PosixStat st;

    if (path.isRoot()) {
        /* Must never fail - we already have the file descriptor for the directory. */
        st = nix::fstat(dirFd.get());
    } else {
        auto [parentFd, parentFdOwning] = openParent(path);
        if (parentFd == INVALID_DESCRIPTOR) {
            if (errno == ENOENT || errno == ENOTDIR)
                return std::nullopt;
            throw SysError("opening directory '%1%'", showPath(path.parent().value()));
        }

        if (dirFdCache && parentFdOwning) {
            assert(*parentFdOwning);
            insertIntoDirFdCache(path.parent().value(), ref<AutoCloseFD>(parentFdOwning));
        }

        /* We know that CanonPath returns a NUL-terminated string_view, so the use of ->data() here is safe. */
        if (::fstatat(parentFd, path.baseName()->data(), &st, AT_SYMLINK_NOFOLLOW) == -1) {
            if (errno == ENOENT)
                return std::nullopt;
            throw SysError("getting status of '%1%'", showPath(path));
        }
    }

    maybeUpdateMtime(st.st_mtime);
    return sourceAccessorStatFromPosixStat(st);
} catch (SymlinkNotAllowed & e) {
    throw SymlinkNotAllowed(e.path, "path '%s' is a symlink", showPath(e.path));
}

void PosixDirectorySourceAccessor::readFile(const CanonPath & path, Sink & sink, fun<void(uint64_t)> sizeCallback)
try {
    if (path.isRoot())
        throw NotARegularFile("'%s' is not a regular file", showPath(path));

    AutoCloseFD fileFd =
        openFileEnsureBeneathNoSymlinks(dirFd.get(), path, O_RDONLY | O_CLOEXEC, /*mode=*/0, makeDirFdCallback());

    if (!fileFd) {
        if (errno == ENOENT || errno == ENOTDIR) /* Intermediate component might not exist. */
            throw FileNotFound("file '%s' does not exist", showPath(path));
        throw SysError("opening '%s'", showPath(path));
    }

    auto st = nix::fstat(fileFd.get());
    if (!S_ISREG(st.st_mode))
        throw Error("file '%s' has an unsupported type", showPath(path));
    PosixFileSourceAccessor fileAccessor(std::move(fileFd), fsPath / path.rel(), trackLastModified, st);
    maybeUpdateMtime(st.st_mtime);
    fileAccessor.readFile(CanonPath::root, sink, sizeCallback);
} catch (SymlinkNotAllowed & e) {
    throw SymlinkNotAllowed(e.path, "path '%s' is a symlink", showPath(e.path));
}

AutoCloseFD PosixDirectorySourceAccessor::openSubdirectory(const CanonPath & path)
try {
    AutoCloseFD dirFdOwning;

    if (path.isRoot()) {
        /* Get a fresh file descriptor for thread-safety. */
        dirFdOwning = ::openat(dirFd.get(), ".", O_DIRECTORY | O_RDONLY | O_CLOEXEC);
        if (!dirFdOwning)
            throw SysError("opening directory '%s'", showPath(path));
    } else {
        dirFdOwning = openFileEnsureBeneathNoSymlinks(
            dirFd.get(), path, O_DIRECTORY | O_RDONLY | O_CLOEXEC, /*mode=*/0, makeDirFdCallback());

        if (!dirFdOwning) {
            if (errno == ENOTDIR)
                throw NotADirectory("'%s' is not a directory", showPath(path));
            throw SysError("opening directory '%s'", showPath(path));
        }
    }

    return dirFdOwning;
} catch (SymlinkNotAllowed & e) {
    throw SymlinkNotAllowed(e.path, "path '%s' is a symlink", showPath(e.path));
}

SourceAccessor::DirEntries PosixDirectorySourceAccessor::readDirectory(const CanonPath & path)
try {
    AutoCloseFD dirFdOwning = openSubdirectory(path);
    AutoCloseDir dir(::fdopendir(dirFdOwning.get()));
    if (!dir)
        throw SysError("reading directory '%s'", showPath(path));
    dirFdOwning.release();

    DirEntries entries;
    const ::dirent * dirent = nullptr;

    while (errno = 0, dirent = ::readdir(dir.get())) {
        checkInterrupt();
        std::string_view name(dirent->d_name);
        if (name == "." || name == "..")
            continue;

        std::optional<Type> type;
        switch (dirent->d_type) {
        case DT_REG:
            type = tRegular;
            break;
        case DT_DIR:
            type = tDirectory;
            break;
        case DT_LNK:
            type = tSymlink;
            break;
        case DT_CHR:
            type = tChar;
            break;
        case DT_BLK:
            type = tBlock;
            break;
        case DT_FIFO:
            type = tFifo;
            break;
        case DT_SOCK:
            type = tSocket;
            break;
        default:
            type = std::nullopt;
            break;
        }
        entries.emplace(name, type);
    }

    if (errno)
        throw SysError("reading directory '%1%'", showPath(path));

    return entries;
} catch (SymlinkNotAllowed & e) {
    throw SymlinkNotAllowed(e.path, "path '%s' is a symlink", showPath(e.path));
}

void PosixDirectorySourceAccessor::readDirectory(
    const CanonPath & dirPath,
    std::function<void(SourceAccessor & subdirAccessor, const CanonPath & subdirRelPath)> callback)
{
    auto fd = openSubdirectory(dirPath);
    PosixDirectorySourceAccessor accessor{
        std::move(fd), fsPath / dirPath.rel(), trackLastModified, /*dirFdCacheSize=*/0};
    callback(accessor, CanonPath::root);
    PosixSourceAccessorBase::maybeUpdateMtime(accessor.mtime);
}

std::string PosixDirectorySourceAccessor::readLink(const CanonPath & path)
try {
    if (path.isRoot())
        throw NotASymlink("file '%s' is not a symlink", showPath(path));

    auto [parentFd, parentFdOwning] = openParent(path);
    if (parentFd == INVALID_DESCRIPTOR) {
        if (errno == ENOENT || errno == ENOTDIR)
            throw FileNotFound("path '%s' does not exist", showPath(path));
        throw SysError("opening directory '%1%'", showPath(path.parent().value()));
    }

    if (dirFdCache && parentFdOwning) {
        assert(*parentFdOwning);
        insertIntoDirFdCache(path.parent().value(), ref<AutoCloseFD>(parentFdOwning));
    }

    try {
        return readLinkAt(parentFd, CanonPath(path.baseName().value()));
    } catch (SysError & e) {
        if (e.errNo == EINVAL)
            throw NotASymlink("file '%s' is not a symlink", showPath(path));
        throw;
    }
} catch (SymlinkNotAllowed & e) {
    throw SymlinkNotAllowed(e.path, "path '%s' is a symlink", showPath(e.path));
}

#else

/**
 * A source accessor that uses the Windows filesystem.
 * @todo Should be moved into a separate file.
 */
class WindowsSourceAccessor : public detail::PosixSourceAccessorBase
{
    /**
     * Optional root path to prefix all operations into the native file
     * system. This allows prepending funny things like `C:\` that
     * `CanonPath` intentionally doesn't support.
     */
    const std::filesystem::path root;

public:

    WindowsSourceAccessor();
    WindowsSourceAccessor(std::filesystem::path && root, bool trackLastModified = false);

    void readFile(const CanonPath & path, Sink & sink, fun<void(uint64_t)> sizeCallback) override;

    using SourceAccessor::readFile;

    bool pathExists(const CanonPath & path) override;

    std::optional<Stat> maybeLstat(const CanonPath & path) override;

    DirEntries readDirectory(const CanonPath & path) override;

    std::string readLink(const CanonPath & path) override;

    std::optional<std::filesystem::path> getPhysicalPath(const CanonPath & path) override;

private:

    /**
     * Throw an error if `path` or any of its ancestors are symlinks.
     */
    void assertNoSymlinks(CanonPath path);

    std::optional<PosixStat> cachedLstat(const CanonPath & path);

    std::filesystem::path makeAbsPath(const CanonPath & path);
};

WindowsSourceAccessor::WindowsSourceAccessor(std::filesystem::path && argRoot, bool trackLastModified)
    : PosixSourceAccessorBase(trackLastModified)
    , root(std::move(argRoot))
{
    assert(root.empty() || root.is_absolute());
    displayPrefix = root.string();
}

WindowsSourceAccessor::WindowsSourceAccessor()
    : WindowsSourceAccessor(std::filesystem::path{})
{
}

std::filesystem::path WindowsSourceAccessor::makeAbsPath(const CanonPath & path)
{
    return root.empty()    ? (std::filesystem::path{path.abs()})
           : path.isRoot() ? /* Don't append a slash for the root of the accessor, since
                                it can be a non-directory (e.g. in the case of `fetchTree
                                { type = "file" }`). */
               root
                           : root / path.rel();
}

void WindowsSourceAccessor::readFile(const CanonPath & path, Sink & sink, fun<void(uint64_t)> sizeCallback)
{
    assertNoSymlinks(path);

    auto ap = makeAbsPath(path);

    AutoCloseFD fd = toDescriptor(open(ap.string().c_str(), O_RDONLY));
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

bool WindowsSourceAccessor::pathExists(const CanonPath & path)
{
    if (auto parent = path.parent())
        assertNoSymlinks(*parent);
    return nix::pathExists(makeAbsPath(path).string());
}

using Cache = boost::concurrent_flat_map<std::string, std::optional<PosixStat>>;
static Cache cache;

std::optional<PosixStat> WindowsSourceAccessor::cachedLstat(const CanonPath & path)
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

std::optional<SourceAccessor::Stat> WindowsSourceAccessor::maybeLstat(const CanonPath & path)
{
    if (auto parent = path.parent())
        assertNoSymlinks(*parent);
    auto st = cachedLstat(path);
    if (!st)
        return std::nullopt;

    PosixSourceAccessorBase::maybeUpdateMtime(st->st_mtime);
    return sourceAccessorStatFromPosixStat(*st);
}

SourceAccessor::DirEntries WindowsSourceAccessor::readDirectory(const CanonPath & path)
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

std::string WindowsSourceAccessor::readLink(const CanonPath & path)
{
    if (auto parent = path.parent())
        assertNoSymlinks(*parent);
    return nix::readLink(makeAbsPath(path)).string();
}

std::optional<std::filesystem::path> WindowsSourceAccessor::getPhysicalPath(const CanonPath & path)
{
    return makeAbsPath(path);
}

void WindowsSourceAccessor::assertNoSymlinks(CanonPath path)
{
    while (!path.isRoot()) {
        auto st = cachedLstat(path);
        if (st && S_ISLNK(st->st_mode))
            throw SymlinkNotAllowed(path, "path '%s' is a symlink", showPath(path));
        path.pop();
    }
}

#endif

} // namespace

ref<SourceAccessor> getFSSourceAccessor()
{
    static auto rootFS = makeFSSourceAccessor("/", /*trackLastModified=*/false);
    return rootFS;
}

ref<SourceAccessor> makeFSSourceAccessor(std::filesystem::path root, bool trackLastModified, FinalSymlink finalSymlink)
{
#ifndef _WIN32
    assert(root.is_absolute());
    AutoCloseFD fd = openFileReadonly(root, finalSymlink);

    if (!fd) {
        if (finalSymlink == FinalSymlink::Follow || errno != ELOOP)
            throw NativeSysError("opening file %1%", PathFmt(root));

        /* A helper class that holds the symlink destination in memory. */
        class SymlinkSourceAccessor : public MemorySourceAccessor
        {
            bool trackLastModified;
            std::time_t mtime;
            std::filesystem::path fsPath;

        public:
            SymlinkSourceAccessor(
                std::string target, std::filesystem::path fsPath_, bool trackLastModified, std::time_t mtime)
                : trackLastModified(trackLastModified)
                , mtime(mtime)
                , fsPath(std::move(fsPath_))
            {
                MemorySink sink{*this};
                sink.createSymlink(CanonPath::root, target);
                displayPrefix = fsPath.native();
            }

            std::optional<std::time_t> getLastModified() override
            {
                return trackLastModified ? std::optional{mtime} : std::nullopt;
            }

            std::optional<std::filesystem::path> getPhysicalPath(const CanonPath & path) override
            {
                if (path.isRoot())
                    return fsPath;
                return fsPath / path.rel(); /* RHS must be a relative path. */
            }

            std::string showPath(const CanonPath & path) override
            {
                /* When rendering the file itself omit the trailing slash. */
                return path.isRoot() ? displayPrefix : SourceAccessor::showPath(path);
            }
        };

        assert(root.has_parent_path());
        assert(root.has_filename());

        /* This branch is taken either if the final component is a symlink
           or we hit a symlink loop. If that's the latter this will also
           throw. This can be done with O_PATH descriptor for the symlink
           itself, but it's not portable. Note that file must have a parent directory
           (it's required to be absolute and root directories can't fail with ELOOP). */

        auto parentFd = openDirectory(root.parent_path(), FinalSymlink::Follow);
        std::time_t mtime = 0;
        if (!parentFd)
            throw SysError("opening %1%", PathFmt(root));

        auto relPath = CanonPath::fromFilename(root.filename().native());
        if (trackLastModified) {
            auto st = fstatat(parentFd.get(), root.filename());
            mtime = st.st_mtime;
        }

        auto linkTarget = readLinkAt(parentFd.get(), relPath);
        return make_ref<SymlinkSourceAccessor>(std::move(linkTarget), std::move(root), trackLastModified, mtime);
    }

    auto st = nix::fstat(fd.get());
    if (S_ISREG(st.st_mode))
        return make_ref<PosixFileSourceAccessor>(std::move(fd), std::move(root), trackLastModified, st);

    else if (S_ISDIR(st.st_mode)) {
        auto res = make_ref<PosixDirectorySourceAccessor>(
            std::move(fd), std::move(root), trackLastModified, PosixDirectorySourceAccessor::getGlobalFdLimit() / 8);
        PosixDirectorySourceAccessor::registerAccessor(res);
        return res;
    }

    else
        throw Error("file %1% has an unsupported type", PathFmt(root));
#else
    return make_ref<WindowsSourceAccessor>(std::move(root), trackLastModified);
#endif
}

} // namespace nix
