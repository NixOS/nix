#pragma once

#include "canon-path.hh"
#include "hash.hh"

namespace nix {

/**
 * A read-only filesystem abstraction. This is used by the Nix
 * evaluator and elsewhere for accessing sources in various
 * filesystem-like entities (such as the real filesystem, tarballs or
 * Git repositories).
 */
struct SourceAccessor
{
    const size_t number;

    SourceAccessor();

    virtual ~SourceAccessor()
    { }

    virtual std::string readFile(const CanonPath & path) = 0;

    virtual bool pathExists(const CanonPath & path) = 0;

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
        //uint64_t fileSize = 0; // regular files only
        bool isExecutable = false; // regular files only
    };

    virtual Stat lstat(const CanonPath & path) = 0;

    std::optional<Stat> maybeLstat(const CanonPath & path);

    typedef std::optional<Type> DirEntry;

    typedef std::map<std::string, DirEntry> DirEntries;

    virtual DirEntries readDirectory(const CanonPath & path) = 0;

    virtual std::string readLink(const CanonPath & path) = 0;

    virtual void dumpPath(
        const CanonPath & path,
        Sink & sink,
        PathFilter & filter = defaultPathFilter);

    Hash hashPath(
        const CanonPath & path,
        PathFilter & filter = defaultPathFilter,
        HashType ht = htSHA256);

    /**
     * Return a corresponding path in the root filesystem, if
     * possible. This is only possible for filesystems that are
     * materialized in the root filesystem.
     */
    virtual std::optional<CanonPath> getPhysicalPath(const CanonPath & path)
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

    /* Return the maximum last-modified time of the files in this
       tree, if available. */
    virtual std::optional<time_t> getLastModified()
    {
        return std::nullopt;
    }
};

/**
 * A source accessor that uses the Unix filesystem.
 */
struct PosixSourceAccessor : SourceAccessor
{
    /**
     * The most recent mtime seen by lstat(). This is a hack to
     * support dumpPathAndGetMtime(). Should remove this eventually.
     */
    time_t mtime = 0;

    std::string readFile(const CanonPath & path) override;

    bool pathExists(const CanonPath & path) override;

    Stat lstat(const CanonPath & path) override;

    DirEntries readDirectory(const CanonPath & path) override;

    std::string readLink(const CanonPath & path) override;

    std::optional<CanonPath> getPhysicalPath(const CanonPath & path) override;
};

}
