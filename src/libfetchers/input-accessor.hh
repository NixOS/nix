#pragma once

#include "ref.hh"
#include "types.hh"
#include "archive.hh"
#include "canon-path.hh"
#include "repair-flag.hh"

namespace nix {

class StorePath;
class Store;

struct InputAccessor
{
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

    typedef std::optional<Type> DirEntry;

    typedef std::map<std::string, DirEntry> DirEntries;
};

/**
 * An abstraction for accessing source files during
 * evaluation. Currently, it's just a wrapper around `CanonPath` that
 * accesses files in the regular filesystem, but in the future it will
 * support fetching files in other ways.
 */
struct SourcePath
{
    CanonPath path;

    SourcePath(CanonPath path)
        : path(std::move(path))
    { }

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
    { return nix::readFile(path.abs()); }

    /**
     * Return whether this `SourcePath` denotes a file (of any type)
     * that exists
    */
    bool pathExists() const
    { return nix::pathExists(path.abs()); }

    /**
     * Return stats about this `SourcePath`, or throw an exception if
     * it doesn't exist.
     */
    InputAccessor::Stat lstat() const;

    /**
     * Return stats about this `SourcePath`, or std::nullopt if it
     * doesn't exist.
     */
    std::optional<InputAccessor::Stat> maybeLstat() const;

    /**
     * If this `SourcePath` denotes a directory (not a symlink),
     * return its directory entries; otherwise throw an error.
     */
    InputAccessor::DirEntries readDirectory() const;

    /**
     * If this `SourcePath` denotes a symlink, return its target;
     * otherwise throw an error.
     */
    std::string readLink() const
    { return nix::readLink(path.abs()); }

    /**
     * Dump this `SourcePath` to `sink` as a NAR archive.
     */
    void dumpPath(
        Sink & sink,
        PathFilter & filter = defaultPathFilter) const
    { return nix::dumpPath(path.abs(), sink, filter); }

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
    { return path; }

    std::string to_string() const
    { return path.abs(); }

    /**
     * Append a `CanonPath` to this path.
     */
    SourcePath operator + (const CanonPath & x) const
    { return {path + x}; }

    /**
     * Append a single component `c` to this path. `c` must not
     * contain a slash. A slash is implicitly added between this path
     * and `c`.
     */
    SourcePath operator + (std::string_view c) const
    {  return {path + c}; }

    bool operator == (const SourcePath & x) const
    {
        return path == x.path;
    }

    bool operator != (const SourcePath & x) const
    {
        return path != x.path;
    }

    bool operator < (const SourcePath & x) const
    {
        return path < x.path;
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
