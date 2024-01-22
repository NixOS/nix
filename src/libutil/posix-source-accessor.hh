#pragma once

#include "source-accessor.hh"

namespace nix {

/**
 * A source accessor that uses the Unix filesystem.
 */
struct PosixSourceAccessor : virtual SourceAccessor
{
    /**
     * The most recent mtime seen by lstat(). This is a hack to
     * support dumpPathAndGetMtime(). Should remove this eventually.
     */
    time_t mtime = 0;

    void readFile(
        const CanonPath & path,
        Sink & sink,
        std::function<void(uint64_t)> sizeCallback) override;

    bool pathExists(const CanonPath & path) override;

    std::optional<Stat> maybeLstat(const CanonPath & path) override;

    DirEntries readDirectory(const CanonPath & path) override;

    std::string readLink(const CanonPath & path) override;

    std::optional<CanonPath> getPhysicalPath(const CanonPath & path) override;

private:

    /**
     * Throw an error if `path` or any of its ancestors are symlinks.
     */
    void assertNoSymlinks(CanonPath path);

    std::optional<struct stat> cachedLstat(const CanonPath & path);
};

}
