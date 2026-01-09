#pragma once
///@file

#include <cassert>
#include <mutex>

#include "nix/util/lru-cache.hh"
#include "nix/util/signals.hh"
#include "nix/util/source-accessor.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/sync.hh"

namespace nix {
namespace unix {

/* The accessors for file/directory access are different, because we want them
   all to work with file descriptors. Technically that could be done on Linux using
   O_PATH descriptors, but that wouldn't work on Darwin. */

class UnixSourceAccessorBase : public SourceAccessor
{
protected:
    bool trackLastModified;
    /**
     * The most recent mtime seen by fstat(). This is a hack to
     * support dumpPathAndGetMtime(). Should remove this eventually.
     */
    std::time_t mtime = 0;

    UnixSourceAccessorBase(bool trackLastModified)
        : trackLastModified(trackLastModified)
    {
    }

    void updateMtime(std::time_t newMtime)
    {
        /* The contract is that trackLastModified implies that the caller uses the accessor
           from a single thread. Thus this is not a CAS loop. */
        if (trackLastModified)
            mtime = std::max(mtime, newMtime);
    }

public:
    std::optional<std::time_t> getLastModified() override
    {
        return trackLastModified ? std::optional{mtime} : std::nullopt;
    }
};

class UnixFileSourceAccessor : public UnixSourceAccessorBase
{
    AutoCloseFD fd;
    CanonPath rootPath;
    mutable std::once_flag statFlag;
    mutable struct ::stat cachedStat;

public:

    UnixFileSourceAccessor(AutoCloseFD fd_, CanonPath rootPath_, bool trackLastModified, struct ::stat * st = nullptr)
        : UnixSourceAccessorBase(trackLastModified)
        , fd(std::move(fd_))
        , rootPath(std::move(rootPath_))
    {
        displayPrefix = rootPath.abs();
        if (st) {
            std::call_once(statFlag, [this, st] {
                cachedStat = *st;
                updateMtime(cachedStat.st_mtime);
            });
        }
    }

    std::string showPath(const CanonPath & path) override
    {
        if (path.isRoot())
            return displayPrefix; /* No trailing slash, we know it's not a directory. */
        return SourceAccessor::showPath(path);
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        if (!path.isRoot())
            throw FileNotFound("path '%s' does not exist", showPath(path));
        throw NotADirectory("path '%s' is not a directory", showPath(path));
    }

    std::string readLink(const CanonPath & path) override
    {
        if (!path.isRoot())
            throw FileNotFound("path '%s' does not exist", showPath(path));
        throw NotASymlink("path '%s' is not a symlink", showPath(path));
    }

    bool pathExists(const CanonPath & path) override
    {
        return path.isRoot(); /* We know that we are accessing a regular file and not a directory. */
    }

    std::optional<std::filesystem::path> getPhysicalPath(const CanonPath & path) override
    {
        if (path.isRoot())
            return std::filesystem::path(rootPath.abs());
        /* Slightly different than what PosixSourceAccessor used to do, but we know that this is not a directory. */
        return std::nullopt;
    }

    std::optional<Stat> maybeLstat(const CanonPath & path) override;

    void readFile(const CanonPath & path, Sink & sink, std::function<void(uint64_t)> sizeCallback) override;
};

class UnixDirectorySourceAccessor : public UnixSourceAccessorBase
{
    AutoCloseFD fd;
    CanonPath rootPath;
    std::unique_ptr<Sync<LRUCache<CanonPath, ref<AutoCloseFD>>>> dirFdCache;

    /**
     * Get the parent directory of path. The second pair element might be an owning file descriptor
     * if path.parent().isRoot() is false.
     */
    std::pair<Descriptor, std::shared_ptr<AutoCloseFD>> openParent(const CanonPath & path);

    std::function<void(AutoCloseFD, CanonPath)> makeDirFdCallback();

    AutoCloseFD openSubdirectory(const CanonPath & path);

public:
    UnixDirectorySourceAccessor(
        AutoCloseFD fd_, CanonPath rootPath_, bool trackLastModified, unsigned dirFdCacheSize = 128)
        : UnixSourceAccessorBase(trackLastModified)
        , fd(std::move(fd_))
        , rootPath(std::move(rootPath_))
    {
        if (rootPath.isRoot())
            displayPrefix.clear(); /* To avoid the double slash. */
        else
            displayPrefix = rootPath.abs();

        if (dirFdCacheSize)
            dirFdCache = std::make_unique<Sync<LRUCache<CanonPath, ref<AutoCloseFD>>>>(dirFdCacheSize);
    }

    std::optional<std::filesystem::path> getPhysicalPath(const CanonPath & path) override
    {
        if (path.isRoot())
            return std::filesystem::path(rootPath.abs());
        return std::filesystem::path(rootPath.abs()) / path.rel(); /* RHS *must* be a relative path. */
    }

    std::optional<Stat> maybeLstat(const CanonPath & path) override;

    void readFile(const CanonPath & path, Sink & sink, std::function<void(uint64_t)> sizeCallback) override;

    DirEntries readDirectory(const CanonPath & path) override;

    void readDirectory(
        const CanonPath & dirPath,
        std::function<void(SourceAccessor & subdirAccessor, const CanonPath & subdirRelPath)> callback) override;

    std::string readLink(const CanonPath & path) override;
};

} // namespace unix
} // namespace nix
