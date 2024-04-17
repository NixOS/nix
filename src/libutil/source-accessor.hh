#pragma once

#include <filesystem>

#include "canon-path.hh"
#include "hash.hh"

namespace nix {

struct Sink;

/**
 * Note there is a decent chance this type soon goes away because the problem is solved another way.
 * See the discussion in https://github.com/NixOS/nix/pull/9985.
 */
enum class SymlinkResolution {
    /**
     * Resolve symlinks in the ancestors only.
     *
     * Only the last component of the result is possibly a symlink.
     */
    Ancestors,

    /**
     * Resolve symlinks fully, realpath(3)-style.
     *
     * No component of the result will be a symlink.
     */
    Full,
};

/**
 * A read-only filesystem abstraction. This is used by the Nix
 * evaluator and elsewhere for accessing sources in various
 * filesystem-like entities (such as the real filesystem, tarballs or
 * Git repositories).
 */
struct SourceAccessor
{
    const size_t number;

    std::string displayPrefix, displaySuffix;

    SourceAccessor();

    virtual ~SourceAccessor()
    { }

    /**
     * Return the contents of a file as a string.
     *
     * @note Unlike Unix, this method should *not* follow symlinks. Nix
     * by default wants to manipulate symlinks explicitly, and not
     * implictly follow them, as they are frequently untrusted user data
     * and thus may point to arbitrary locations. Acting on the targets
     * targets of symlinks should only occasionally be done, and only
     * with care.
     */
    virtual std::string readFile(const CanonPath & path);

    /**
     * Write the contents of a file as a sink. `sizeCallback` must be
     * called with the size of the file before any data is written to
     * the sink.
     *
     * @note Like the other `readFile`, this method should *not* follow
     * symlinks.
     *
     * @note subclasses of `SourceAccessor` need to implement at least
     * one of the `readFile()` variants.
     */
    virtual void readFile(
        const CanonPath & path,
        Sink & sink,
        std::function<void(uint64_t)> sizeCallback = [](uint64_t size){});

    virtual bool pathExists(const CanonPath & path);

    enum Type {
      tRegular, tSymlink, tDirectory,
      /**
        Any other node types that may be encountered on the file system, such as device nodes, sockets, named pipe, and possibly even more exotic things.

        Responsible for `"unknown"` from `builtins.readFileType "/dev/null"`.

        Unlike `DT_UNKNOWN`, this must not be used for deferring the lookup of types.
      */
      tMisc
    };

    struct Stat
    {
        Type type = tMisc;

        /**
         * For regular files only: the size of the file. Not all
         * accessors return this since it may be too expensive to
         * compute.
         */
        std::optional<uint64_t> fileSize;

        /**
         * For regular files only: whether this is an executable.
         */
        bool isExecutable = false;

        /**
         * For regular files only: the position of the contents of this
         * file in the NAR. Only returned by NAR accessors.
         */
        std::optional<uint64_t> narOffset;
    };

    Stat lstat(const CanonPath & path);

    virtual std::optional<Stat> maybeLstat(const CanonPath & path) = 0;

    typedef std::optional<Type> DirEntry;

    typedef std::map<std::string, DirEntry> DirEntries;

    /**
     * @note Like `readFile`, this method should *not* follow symlinks.
     */
    virtual DirEntries readDirectory(const CanonPath & path) = 0;

    virtual std::string readLink(const CanonPath & path) = 0;

    virtual void dumpPath(
        const CanonPath & path,
        Sink & sink,
        PathFilter & filter = defaultPathFilter);

    Hash hashPath(
        const CanonPath & path,
        PathFilter & filter = defaultPathFilter,
        HashAlgorithm ha = HashAlgorithm::SHA256);

    /**
     * Return a corresponding path in the root filesystem, if
     * possible. This is only possible for filesystems that are
     * materialized in the root filesystem.
     */
    virtual std::optional<std::filesystem::path> getPhysicalPath(const CanonPath & path)
    { return std::nullopt; }

    bool operator == (const SourceAccessor & x) const
    {
        return number == x.number;
    }

    bool operator < (const SourceAccessor & x) const
    {
        return number < x.number;
    }

    void setPathDisplay(std::string displayPrefix, std::string displaySuffix = "");

    virtual std::string showPath(const CanonPath & path);

    /**
     * Resolve any symlinks in `path` according to the given
     * resolution mode.
     *
     * @param mode might only be a temporary solution for this.
     * See the discussion in https://github.com/NixOS/nix/pull/9985.
     */
    CanonPath resolveSymlinks(
        const CanonPath & path,
        SymlinkResolution mode = SymlinkResolution::Full);
};

}
