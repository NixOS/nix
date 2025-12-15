#pragma once

#include "nix/util/source-accessor.hh"

namespace nix {

struct SourcePath;

/**
 * A source accessor that uses the Unix filesystem.
 */
class PosixSourceAccessor : virtual public SourceAccessor
{
    /**
     * Optional root path to prefix all operations into the native file
     * system. This allows prepending funny things like `C:\` that
     * `CanonPath` intentionally doesn't support.
     */
    const std::filesystem::path root;

    const bool trackLastModified = false;

public:

    PosixSourceAccessor();
    PosixSourceAccessor(std::filesystem::path && root, bool trackLastModified = false);

    /**
     * The most recent mtime seen by lstat(). This is a hack to
     * support dumpPathAndGetMtime(). Should remove this eventually.
     */
    time_t mtime = 0;

    void readFile(const CanonPath & path, Sink & sink, std::function<void(uint64_t)> sizeCallback) override;

    bool pathExists(const CanonPath & path) override;

    std::optional<Stat> maybeLstat(const CanonPath & path) override;

    DirEntries readDirectory(const CanonPath & path) override;

    std::string readLink(const CanonPath & path) override;

    std::optional<std::filesystem::path> getPhysicalPath(const CanonPath & path) override;

    std::optional<std::time_t> getLastModified() override
    {
        return trackLastModified ? std::optional{mtime} : std::nullopt;
    }

private:

    /**
     * Throw an error if `path` or any of its ancestors are symlinks.
     */
    void assertNoSymlinks(CanonPath path);

    std::optional<struct stat> cachedLstat(const CanonPath & path);

    std::filesystem::path makeAbsPath(const CanonPath & path);
};

} // namespace nix
