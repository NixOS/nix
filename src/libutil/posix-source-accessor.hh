#pragma once

#include "source-accessor.hh"

namespace nix {

/**
 * A source accessor that uses the Unix filesystem.
 */
struct PosixSourceAccessor : virtual SourceAccessor
{
    /**
     * Optional root path to prefix all operations into the native file
     * system. This allows prepending funny things like `C:\` that
     * `CanonPath` intentionally doesn't support.
     */
    const std::filesystem::path root;

    PosixSourceAccessor();
    PosixSourceAccessor(std::filesystem::path && root);

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

    std::optional<std::filesystem::path> getPhysicalPath(const CanonPath & path) override;

    /**
     * Create a `PosixSourceAccessor` and `CanonPath` corresponding to
     * some native path.
     *
     * The `PosixSourceAccessor` is rooted as far up the tree as
     * possible, (e.g. on Windows it could scoped to a drive like
     * `C:\`). This allows more `..` parent accessing to work.
     *
     * See
     * [`std::filesystem::path::root_path`](https://en.cppreference.com/w/cpp/filesystem/path/root_path)
     * and
     * [`std::filesystem::path::relative_path`](https://en.cppreference.com/w/cpp/filesystem/path/relative_path).
     */
    static std::pair<PosixSourceAccessor, CanonPath> createAtRoot(const std::filesystem::path & path);

private:

    /**
     * Throw an error if `path` or any of its ancestors are symlinks.
     */
    void assertNoSymlinks(CanonPath path);

    std::optional<struct stat> cachedLstat(const CanonPath & path);

    std::filesystem::path makeAbsPath(const CanonPath & path);
};

}
