#include "nix/util/source-accessor.hh"
#include "nix/util/file-system-at.hh"
#include "nix/util/lru-cache.hh"
#include "nix/util/sync.hh"
#include "nix/util/memory-source-accessor.hh"
#include "nix/util/signals.hh"
#include "nix/util/util.hh"
#include "nix/util/system-error.hh"

#include "file-descriptor-private.hh"

#include <atomic>

#ifndef _WIN32
#  include <sys/resource.h>

#  include "unix/unix-modes.hh"
#endif

namespace nix {

/**
 * Whether `openDirectoryTraversable()` can succeed where `openDirectory()`
 * is refused permission, so that retrying with it is worthwhile.
 */
static constexpr bool canTraverseWithoutRead =
#ifdef _WIN32
    /* It asks for just `FILE_TRAVERSE` rather than `FILE_LIST_DIRECTORY`. */
    true
#else
    unix::canTraverseWithoutRead
#endif
    ;

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

static std::optional<SourceAccessor::Type> sourceAccessorTypeFromFileType(std::filesystem::file_type type)
{
    using enum SourceAccessor::Type;
    using T = std::filesystem::file_type;
    switch (type) {
    case T::regular:
        return tRegular;
    case T::directory:
        return tDirectory;
    case T::symlink:
        return tSymlink;
    case T::character:
        return tChar;
    case T::block:
        return tBlock;
    case T::fifo:
        return tFifo;
    case T::socket:
        return tSocket;
    case T::none:
        return std::nullopt;
    case T::not_found:
    case T::unknown:
        return tUnknown;
    }
    unreachable();
}

namespace {

/**
 * Common base helper class for deduplicating common code paths for tracking mtime.
 */
class PosixSourceAccessorBase : virtual public SourceAccessor
{
private:
    void anchor() override;

protected:
    const bool trackLastModified = false;

    /**
     * The most recent mtime seen by lstat(). This is a hack to
     * support dumpPathAndGetMtime(). Should remove this eventually.
     */
    std::atomic<time_t> mtime = 0;

    void maybeUpdateMtime(time_t seenMTime)
    {
        if (trackLastModified) {
            /* Don't have https://en.cppreference.com/cpp/atomic/atomic/fetch_max yet, thus a CAS loop. */
            time_t oldValue = mtime.load(std::memory_order_relaxed);
            while (oldValue < seenMTime && !mtime.compare_exchange_weak(oldValue, seenMTime, std::memory_order_relaxed))
                ;
        }
    }

    PosixSourceAccessorBase(bool trackLastModified)
        : trackLastModified(trackLastModified)
    {
    }

    virtual std::optional<time_t> getLastModified() override
    {
        if (trackLastModified)
            return mtime;
        return std::nullopt;
    }
};

void PosixSourceAccessorBase::anchor() {}

class PosixDirectorySourceAccessor;

class PosixFileSourceAccessor : public PosixSourceAccessorBase
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
    copyFdRange(fd.get(), /*offset=*/0, size, sink, /*tryCoW=*/true);
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
#ifdef _WIN32
    /* No small per-process handle limit like `RLIMIT_NOFILE`, so just use
       the same upper bound as below. */
    return 4096;
#else
    ::rlimit lim{};
    if (::getrlimit(RLIMIT_NOFILE, &lim) == -1)
        throw SysError("querying RLIMIT_NOFILE");
    /* Some sane upper bound in case we have a huge rlimit. */
    return std::min<rlim_t>(4096, lim.rlim_cur / 8);
#endif
}

class PosixDirectorySourceAccessor : public PosixSourceAccessorBase
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

    std::shared_ptr<Sync<LRUCache<OsCanonPath, ref<AutoCloseFD>>>> dirFdCache;

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

    void insertIntoDirFdCache(const OsCanonPath & key, ref<AutoCloseFD> fd)
    {
        assert(dirFdCache);
        auto cache = dirFdCache->lock();
        auto before = cache->size();
        cache->upsert(key, std::move(fd));
        globalDirFdCount.fetch_add(cache->size() - before, std::memory_order_relaxed);
    }

    /**
     * Helper for opening the parent directory used for other FD-relative operations down the line.
     * Also caches the resulting dirFd. Throws FileNotFound if some intermediate directory or the parent
     * doesn't exist.
     */
    std::pair<Descriptor, std::shared_ptr<AutoCloseFD>> openParentAndUpsert(const CanonPath & path, bool ignoreMissing);

    /**
     * Result of `openParent()`.
     */
    struct OpenedParent
    {
        Descriptor fd;

        /**
         * Owning file descriptor, unless `path.parent().isRoot()`.
         */
        std::shared_ptr<AutoCloseFD> fdOwning;

        /**
         * `path.parent()`, so that callers don't recompute it (which copies a string).
         *
         * OS-native, because its main use is as a `dirFdCache` key; convert
         * with `toPortable()` for display.
         */
        std::optional<OsCanonPath> parent;

        /**
         * Whether `fdOwning` came out of `dirFdCache` under exactly `parent`.
         */
        bool fromCache = false;
    };

    /**
     * Get the parent directory of path.
     *
     * @return The opened parent, or on failure the native error code.
     */
    Result<OpenedParent, NativeError> openParent(const CanonPath & path);

    std::function<void(AutoCloseFD, OsCanonPath)> makeDirFdCallback();

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
            dirFdCache = std::make_shared<Sync<LRUCache<OsCanonPath, ref<AutoCloseFD>>>>(dirFdCacheSize);
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

/**
 * The path carried by a `SymlinkNotAllowed` from `file-system-at.hh`, as a
 * `CanonPath` so it can be re-prefixed and fed to `showPath`.
 */
static CanonPath portablePathOf(const SymlinkNotAllowed & e)
{
    return std::visit(
        overloaded{
            [](const CanonPath & p) { return p; },
            [](const OsCanonPath & p) { return p.toPortable(); },
        },
        e.path);
}

/**
 * The last component of a non-root `CanonPath`. `CanonPath` already
 * guarantees it is a plain filename, so asserting is fine.
 */
static OsFilename osBaseNameOf(const CanonPath & path)
{
    return OsFilename::fromPathAsserting(path.baseName().value());
}

std::function<void(AutoCloseFD, OsCanonPath)> PosixDirectorySourceAccessor::makeDirFdCallback()
{
    if (!dirFdCache)
        return nullptr;

    return [this](AutoCloseFD fd, OsCanonPath key) {
        assert(fd);
        insertIntoDirFdCache(std::move(key), make_ref<AutoCloseFD>(std::move(fd)));
    };
}

Result<PosixDirectorySourceAccessor::OpenedParent, NativeError>
PosixDirectorySourceAccessor::openParent(const CanonPath & path)
{
    assert(!path.isRoot());
    /* Everything below is in OS-native terms: these are keys for a cache of
       real directory handles, not positions in the accessor's virtual
       namespace. `path` is the last portable thing we see. */
    OsCanonPath parent = path.parent().value();
    if (parent.empty())
        return outcome::success(OpenedParent{dirFd.get(), nullptr, std::move(parent)});

    maybeEvictFromGlobalCaches();

    std::shared_ptr<AutoCloseFD> intermediateParentFd;
    OsCanonPath anchor;

    if (dirFdCache) {
        auto cache = dirFdCache->lock();
        auto p = parent;
        while (true) {
            if (auto intermediateDirFdHit = cache->get(p)) {
                if (p == parent)
                    return outcome::success(
                        OpenedParent{
                            .fd = (*intermediateDirFdHit)->get(),
                            .fdOwning = *intermediateDirFdHit,
                            .parent = std::move(parent),
                            .fromCache = true,
                        });
                intermediateParentFd = intermediateDirFdHit->get_ptr();
                anchor = p;
                break;
            }
            if (p.empty())
                break;
            p.pop();
        }
    }

    Descriptor startFd = intermediateParentFd ? intermediateParentFd->get() : dirFd.get();
    OsCanonPath relPath = intermediateParentFd ? parent.removePrefix(anchor) : parent;

    std::function<void(AutoCloseFD, OsCanonPath)> cb;
    if (auto base = makeDirFdCallback()) {
        if (intermediateParentFd) {
            cb = [base = std::move(base), prefix = anchor](AutoCloseFD fd, OsCanonPath relKey) {
                base(std::move(fd), prefix / relKey);
            };
        } else {
            cb = std::move(base);
        }
    }

    try {
        auto parentFdOwning = openFileEnsureBeneathNoSymlinks(
            startFd,
            relPath,
#ifdef _WIN32
            FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
            FILE_DIRECTORY_FILE,
            FILE_OPEN,
#else
            /* As to not require read permissions on the directory. */
            unix::traversableDirAccessMode | O_DIRECTORY | O_CLOEXEC,
            0,
#endif
            std::move(cb));
        if (!parentFdOwning)
            return outcome::failure(parentFdOwning.error());
        auto fd = parentFdOwning.value().get();
        return outcome::success(
            OpenedParent{fd, make_ref<AutoCloseFD>(std::move(parentFdOwning).value()), std::move(parent)});
    } catch (SymlinkNotAllowed & e) {
        /* Need to fixup the error message to include the actual path relative to the (possibly) cached fd. */
        auto p = anchor.toPortable() / portablePathOf(e);
        throw SymlinkNotAllowed(p, "path '%s' (or its ancestor) is a symlink", showPath(p));
    }
}

std::pair<Descriptor, std::shared_ptr<AutoCloseFD>>
PosixDirectorySourceAccessor::openParentAndUpsert(const CanonPath & path, bool ignoreMissing)
{
    auto openedOrError = openParent(path);
    if (!openedOrError) {
        auto ec = NativeSysError::toErrorCode(openedOrError.error());
        if (ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory) {
            /* Intermediate component might not exist. */
            if (ignoreMissing)
                return {INVALID_DESCRIPTOR, {}};
            else
                throw FileNotFound("path '%s' does not exist", showPath(path));
        }
        throw NativeSysError(openedOrError.error(), "opening directory '%1%'", showPath(path.parent().value()));
    }
    auto [parentFd, parentFdOwning, parent, fromCache] = std::move(openedOrError).value();

    /* A cached fd needs no upsert: `LRUCache::get()` has already promoted it. */
    if (dirFdCache && parentFdOwning && !fromCache) {
        assert(*parentFdOwning);
        insertIntoDirFdCache(*parent, ref<AutoCloseFD>(parentFdOwning));
    }

    return {parentFd, parentFdOwning};
}

std::optional<SourceAccessor::Stat> PosixDirectorySourceAccessor::maybeLstat(const CanonPath & path)
{
    PosixStat st;

    if (path.isRoot()) {
        /* Must never fail - we already have the file descriptor for the directory. */
        st = nix::fstat(dirFd.get());
    } else {
        auto [parentFd, parentFdOwning] = openParentAndUpsert(path, /*ignoreMissing=*/true);
        if (parentFd == INVALID_DESCRIPTOR)
            return std::nullopt;

        auto maybeSt = maybeFstatat(parentFd, osBaseNameOf(path));
        if (!maybeSt)
            return std::nullopt;
        st = std::move(*maybeSt);
    }

    maybeUpdateMtime(st.st_mtime);
    return sourceAccessorStatFromPosixStat(st);
}

void PosixDirectorySourceAccessor::readFile(const CanonPath & path, Sink & sink, fun<void(uint64_t)> sizeCallback)
{
    if (path.isRoot())
        throw NotARegularFile("'%s' is not a regular file", showPath(path));

    /* TODO: We can do better when we have openat2. */
    auto [parentFd, parentFdOwning] = openParentAndUpsert(path, /*ignoreMissing=*/false);
    AutoCloseFD fileFd;

    try {
        fileFd = valueOr(
            openFileEnsureBeneathNoSymlinks(
                parentFd,
                osBaseNameOf(path),
#ifdef _WIN32
                /* No `FILE_NON_DIRECTORY_FILE`, so that a directory is
                   reported as `NotARegularFile` below, like on Unix. */
                GENERIC_READ,
                0
#else
                O_RDONLY | O_CLOEXEC
#endif
                ),
            [&](auto err) {
                if (NativeSysError::toErrorCode(err) == std::errc::no_such_file_or_directory)
                    throw FileNotFound("file '%s' does not exist", showPath(path));
                throw NativeSysError(err, "opening '%s'", showPath(path));
            });
    } catch (SymlinkNotAllowed & e) {
        auto parent = path.parent().value();
        auto p = parent / portablePathOf(e);
        throw SymlinkNotAllowed(p, "path '%s' is a symlink", showPath(p));
    }

    auto st = nix::fstat(fileFd.get());
    if (!S_ISREG(st.st_mode))
        throw NotARegularFile("file '%s' is not a regular file", showPath(path));
    PosixFileSourceAccessor fileAccessor(std::move(fileFd), fsPath / path.rel(), trackLastModified, st);
    maybeUpdateMtime(st.st_mtime);
    fileAccessor.readFile(CanonPath::root, sink, sizeCallback);
}

AutoCloseFD PosixDirectorySourceAccessor::openSubdirectory(const CanonPath & path)
{
    AutoCloseFD dirFdOwning;

    if (path.isRoot()) {
        /* Get a fresh file descriptor for thread-safety. */
        dirFdOwning = valueOr(
            openResult(
#ifdef _WIN32
                ReOpenFile(
                    dirFd.get(),
                    FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    FILE_FLAG_BACKUP_SEMANTICS)
#else
                ::openat(dirFd.get(), ".", O_DIRECTORY | O_RDONLY | O_CLOEXEC)
#endif
                    ),
            NIX_THROW_NATIVE_SYS_ERROR_HANDLER("opening directory '%s'", showPath(path)));
    } else {
        /* TODO: We can do better when we have openat2. */
        auto [parentFd, parentFdOwning] = openParentAndUpsert(path, /*ignoreMissing=*/false);

        try {
            dirFdOwning = valueOr(
                openFileEnsureBeneathNoSymlinks(
                    parentFd,
                    osBaseNameOf(path),
#ifdef _WIN32
                    FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                    FILE_DIRECTORY_FILE
#else
                    O_DIRECTORY | O_RDONLY | O_CLOEXEC
#endif
                    ),
                [&](auto err) {
                    if (NativeSysError::toErrorCode(err) == std::errc::not_a_directory)
                        throw NotADirectory("'%s' is not a directory", showPath(path));
                    throw NativeSysError(err, "opening directory '%s'", showPath(path));
                });
        } catch (SymlinkNotAllowed & e) {
            auto parent = path.parent().value();
            auto p = parent / portablePathOf(e);
            throw SymlinkNotAllowed(p, "path '%s' is a symlink", showPath(p));
        }
    }

    return dirFdOwning;
}

SourceAccessor::DirEntries PosixDirectorySourceAccessor::readDirectory(const CanonPath & path)
try {
    DirEntries entries;
    /* The read position lives in the handle, which is fine because
       `openSubdirectory()` always gives us a fresh one. */
    for (const auto & entry : DirectoryIterator{openSubdirectory(path)}) {
        checkInterrupt();
        entries.emplace(os_string_to_string(entry.name.path().native()), sourceAccessorTypeFromFileType(entry.type));
    }
    return entries;
} catch (SymlinkNotAllowed & e) {
    auto p = portablePathOf(e);
    throw SymlinkNotAllowed(p, "path '%s' is a symlink", showPath(p));
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

    auto [parentFd, parentFdOwning] = openParentAndUpsert(path, /*ignoreMissing=*/false);

    try {
        return os_string_to_string(readLinkAt(parentFd, osBaseNameOf(path)));
    } catch (NativeSysError & e) {
#ifdef _WIN32
        if (e.lastError == ERROR_NOT_A_REPARSE_POINT || e.lastError == ERROR_REPARSE_TAG_INVALID)
#else
        if (e.errNo == EINVAL)
#endif
            throw NotASymlink("file '%s' is not a symlink", showPath(path));
        throw;
    }
} catch (SymlinkNotAllowed & e) {
    auto p = portablePathOf(e);
    throw SymlinkNotAllowed(p, "path '%s' is a symlink", showPath(p));
}

} // namespace

ref<SourceAccessor> getFSSourceAccessor()
{
    static auto rootFS = makeFSSourceAccessor("/", /*trackLastModified=*/false);
    return rootFS;
}

ref<SourceAccessor> makeFSSourceAccessor(std::filesystem::path root, bool trackLastModified, FinalSymlink finalSymlink)
{
    assert(root.is_absolute());
    auto fd = openFileReadonly(root, finalSymlink);

    if (canTraverseWithoutRead && !fd && NativeSysError::toErrorCode(fd.error()) == std::errc::permission_denied) {
        /* `root` may be a directory we can traverse but not list (e.g.
           mode 0111). The directory accessor only uses its descriptor as
           the base for `*at` calls and `fstat`, which a traversable
           descriptor suffices for, so don't require read permission.
           Listing `root` itself reopens it for that, and so still fails
           as it should.

           This can happen on Windows too, when we are granted
           `FILE_TRAVERSE` but not `FILE_LIST_DIRECTORY` on `root`, though
           not with `SeBackupPrivilege` enabled, since
           `FILE_FLAG_BACKUP_SEMANTICS` then bypasses the access check. */
        /* Not a directory after all: keep the original error. */
        if (auto traversable = openDirectoryTraversable(root, finalSymlink))
            fd = std::move(traversable);
    }

#ifdef _WIN32
    if (!fd)
        throw NativeSysError(fd.error(), "opening file %1%", PathFmt(root));

    /* With `FinalSymlink::DontFollow`, Windows opens a final symlink
       itself, rather than refusing to as Unix does, so check its status. */
    auto st = nix::fstat(fd.value().get());
    if (S_ISLNK(st.st_mode)) {
#else
    if (!fd) {
        if (finalSymlink == FinalSymlink::Follow || fd.error() != NIX_ERR_OPEN_SYMLINK)
            throw NativeSysError(fd.error(), "opening file %1%", PathFmt(root));
#endif

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
                displayPrefix = fsPath.string();
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

        auto parentFd = valueOr(
            openDirectory(root.parent_path(), FinalSymlink::Follow),
            NIX_THROW_NATIVE_SYS_ERROR_HANDLER("opening %1%", PathFmt(root)));
        std::time_t mtime = 0;

        auto relPath = OsFilename::fromPathAsserting(root.filename());
        if (trackLastModified) {
            /* On Windows we already have the symlink's own status, from the
               handle to the symlink itself. */
#ifndef _WIN32
            auto st = fstatat(parentFd.get(), relPath);
#endif
            mtime = st.st_mtime;
        }

        auto linkTarget = os_string_to_string(readLinkAt(parentFd.get(), relPath));
        return make_ref<SymlinkSourceAccessor>(std::move(linkTarget), std::move(root), trackLastModified, mtime);
    }

#ifndef _WIN32
    auto st = nix::fstat(fd.value().get());
#endif
    if (S_ISREG(st.st_mode))
        return make_ref<PosixFileSourceAccessor>(std::move(fd).value(), std::move(root), trackLastModified, st);

    else if (S_ISDIR(st.st_mode)) {
        auto res = make_ref<PosixDirectorySourceAccessor>(
            std::move(fd).value(),
            std::move(root),
            trackLastModified,
            PosixDirectorySourceAccessor::getGlobalFdLimit() / 8);
        PosixDirectorySourceAccessor::registerAccessor(res);
        return res;
    }

    else
        throw Error("file %1% has an unsupported type", PathFmt(root));
}

} // namespace nix
