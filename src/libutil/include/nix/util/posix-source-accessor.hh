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

} // namespace nix
