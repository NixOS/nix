#pragma once

#include "nix/util/source-accessor.hh"

namespace nix {

struct SourcePath;

namespace detail {

/**
 * Common base helper class for deduplicating common code paths for tracking mtime.
 */
class PosixSourceAccessorBase : virtual public SourceAccessor
{
protected:
    const bool trackLastModified = false;

    /**
     * The most recent mtime seen by lstat(). This is a hack to
     * support dumpPathAndGetMtime(). Should remove this eventually.
     */
    time_t mtime = 0;

    void maybeUpdateMtime(time_t seenMTime)
    {
        /* The contract is that trackLastModified implies that the caller uses the accessor
           from a single thread. Thus this is not a CAS loop. */
        if (trackLastModified)
            mtime = std::max(mtime, seenMTime);
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

} // namespace detail

/**
 * A source accessor that uses the Unix filesystem.
 */
class PosixSourceAccessor : public detail::PosixSourceAccessorBase
{
    /**
     * Optional root path to prefix all operations into the native file
     * system. This allows prepending funny things like `C:\` that
     * `CanonPath` intentionally doesn't support.
     */
    const std::filesystem::path root;

public:

    PosixSourceAccessor();
    PosixSourceAccessor(std::filesystem::path && root, bool trackLastModified = false);

    void readFile(const CanonPath & path, Sink & sink, fun<void(uint64_t)> sizeCallback) override;

    using SourceAccessor::readFile;

    bool pathExists(const CanonPath & path) override;

    std::optional<Stat> maybeLstat(const CanonPath & path) override;

    DirEntries readDirectory(const CanonPath & path) override;

    std::string readLink(const CanonPath & path) override;

    std::optional<std::filesystem::path> getPhysicalPath(const CanonPath & path) override;

    void invalidateCache(const CanonPath & path) override;

private:

    /**
     * Throw an error if `path` or any of its ancestors are symlinks.
     */
    void assertNoSymlinks(CanonPath path);

    std::optional<PosixStat> cachedLstat(const CanonPath & path);

    std::filesystem::path makeAbsPath(const CanonPath & path);
};

} // namespace nix
