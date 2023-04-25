#pragma once

#include "ref.hh"
#include "types.hh"
#include "archive.hh"
#include "canon-path.hh"
#include "repair-flag.hh"
#include "hash.hh"

namespace nix {

MakeError(RestrictedPathError, Error);

struct SourcePath;
class StorePath;
class Store;

struct InputAccessor : public std::enable_shared_from_this<InputAccessor>
{
    const size_t number;

    std::string displayPrefix, displaySuffix;

    std::optional<std::string> fingerprint;

    InputAccessor();

    virtual ~InputAccessor()
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

    StorePath fetchToStore(
        ref<Store> store,
        const CanonPath & path,
        std::string_view name = "source",
        PathFilter * filter = nullptr,
        RepairFlag repair = NoRepair);

    /* Return a corresponding path in the root filesystem, if
       possible. This is only possible for inputs that are
       materialized in the root filesystem. */
    virtual std::optional<CanonPath> getPhysicalPath(const CanonPath & path)
    { return std::nullopt; }

    bool operator == (const InputAccessor & x) const
    {
        return number == x.number;
    }

    bool operator < (const InputAccessor & x) const
    {
        return number < x.number;
    }

    void setPathDisplay(std::string displayPrefix, std::string displaySuffix = "");

    virtual std::string showPath(const CanonPath & path);

    SourcePath root();

    /* Return the maximum last-modified time of the files in this
       tree, if available. */
    virtual std::optional<time_t> getLastModified()
    {
        return std::nullopt;
    }
};

typedef std::function<RestrictedPathError(const CanonPath & path)> MakeNotAllowedError;

struct SourcePath;

struct MemoryInputAccessor : InputAccessor
{
    virtual SourcePath addFile(CanonPath path, std::string && contents) = 0;
};

ref<MemoryInputAccessor> makeMemoryInputAccessor();

ref<InputAccessor> makeZipInputAccessor(const CanonPath & path);

ref<InputAccessor> makePatchingInputAccessor(
    ref<InputAccessor> next,
    const std::vector<std::string> & patches);
/**
 * An abstraction for accessing source files during
 * evaluation. Currently, it's just a wrapper around `CanonPath` that
 * accesses files in the regular filesystem, but in the future it will
 * support fetching files in other ways.
 */
struct SourcePath
{
    ref<InputAccessor> accessor;
    CanonPath path;

    std::string_view baseName() const;

    /**
     * Construct the parent of this `SourcePath`. Aborts if `this`
     * denotes the root.
     */
    SourcePath parent() const;

    /**
     * If this `SourcePath` denotes a regular file (not a symlink),
     * return its contents; otherwise throw an error.
     */
    std::string readFile() const
    { return accessor->readFile(path); }

    /**
     * Return whether this `SourcePath` denotes a file (of any type)
     * that exists
    */
    bool pathExists() const
    { return accessor->pathExists(path); }

    /**
     * Return stats about this `SourcePath`, or throw an exception if
     * it doesn't exist.
     */
    InputAccessor::Stat lstat() const
    { return accessor->lstat(path); }

    /**
     * Return stats about this `SourcePath`, or std::nullopt if it
     * doesn't exist.
     */
    std::optional<InputAccessor::Stat> maybeLstat() const
    { return accessor->maybeLstat(path); }

    /**
     * If this `SourcePath` denotes a directory (not a symlink),
     * return its directory entries; otherwise throw an error.
     */
    InputAccessor::DirEntries readDirectory() const
    { return accessor->readDirectory(path); }

    /**
     * If this `SourcePath` denotes a symlink, return its target;
     * otherwise throw an error.
     */
    std::string readLink() const
    { return accessor->readLink(path); }

    /**
     * Dump this `SourcePath` to `sink` as a NAR archive.
     */
    void dumpPath(
        Sink & sink,
        PathFilter & filter = defaultPathFilter) const
    { return accessor->dumpPath(path, sink, filter); }

    /**
     * Copy this `SourcePath` to the Nix store.
     */
    StorePath fetchToStore(
        ref<Store> store,
        std::string_view name = "source",
        PathFilter * filter = nullptr,
        RepairFlag repair = NoRepair) const;

    /**
     * Return the location of this path in the "real" filesystem, if
     * it has a physical location.
     */
    std::optional<CanonPath> getPhysicalPath() const
    { return accessor->getPhysicalPath(path); }

    std::string to_string() const
    { return accessor->showPath(path); }

    /**
     * Append a `CanonPath` to this path.
     */
    SourcePath operator + (const CanonPath & x) const
    { return {accessor, path + x}; }

    /**
     * Append a single component `c` to this path. `c` must not
     * contain a slash. A slash is implicitly added between this path
     * and `c`.
     */
    SourcePath operator + (std::string_view c) const
    {  return {accessor, path + c}; }

    bool operator == (const SourcePath & x) const
    {
        return std::tie(accessor, path) == std::tie(x.accessor, x.path);
    }

    bool operator != (const SourcePath & x) const
    {
        return std::tie(accessor, path) != std::tie(x.accessor, x.path);
    }

    bool operator < (const SourcePath & x) const
    {
        return std::tie(accessor, path) < std::tie(x.accessor, x.path);
    }

    /**
     * Resolve any symlinks in this `SourcePath` (including its
     * parents). The result is a `SourcePath` in which no element is a
     * symlink.
     */
    SourcePath resolveSymlinks() const;
};

std::ostream & operator << (std::ostream & str, const SourcePath & path);

}
