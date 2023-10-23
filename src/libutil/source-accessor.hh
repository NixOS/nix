#pragma once

#include "canon-path.hh"
#include "hash.hh"

namespace nix {

struct Sink;

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
     */
    virtual std::string readFile(const CanonPath & path);

    /**
     * Write the contents of a file as a sink. `sizeCallback` must be
     * called with the size of the file before any data is written to
     * the sink.
     *
     * Note: subclasses of `SourceAccessor` need to implement at least
     * one of the `readFile()` variants.
     */
    virtual void readFile(
        const CanonPath & path,
        Sink & sink,
        std::function<void(uint64_t)> sizeCallback = [](uint64_t size){});

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
};

}
