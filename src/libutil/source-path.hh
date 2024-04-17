#pragma once
/**
 * @file
 *
 * @brief SourcePath
 */

#include "ref.hh"
#include "canon-path.hh"
#include "input-accessor.hh"

namespace nix {

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

    SourcePath(ref<InputAccessor> accessor, CanonPath path = CanonPath::root)
        : accessor(std::move(accessor))
        , path(std::move(path))
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
    std::string readFile() const;

    /**
     * Return whether this `SourcePath` denotes a file (of any type)
     * that exists
    */
    bool pathExists() const;

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
    std::string readLink() const;

    /**
     * Dump this `SourcePath` to `sink` as a NAR archive.
     */
    void dumpPath(
        Sink & sink,
        PathFilter & filter = defaultPathFilter) const;

    /**
     * Return the location of this path in the "real" filesystem, if
     * it has a physical location.
     */
    std::optional<std::filesystem::path> getPhysicalPath() const;

    std::string to_string() const;

    /**
     * Append a `CanonPath` to this path.
     */
    SourcePath operator / (const CanonPath & x) const;

    /**
     * Append a single component `c` to this path. `c` must not
     * contain a slash. A slash is implicitly added between this path
     * and `c`.
     */
    SourcePath operator / (std::string_view c) const;

    bool operator==(const SourcePath & x) const;
    bool operator!=(const SourcePath & x) const;
    bool operator<(const SourcePath & x) const;

    /**
     * Convenience wrapper around `SourceAccessor::resolveSymlinks()`.
     */
    SourcePath resolveSymlinks(
        SymlinkResolution mode = SymlinkResolution::Full) const
    {
        return {accessor, accessor->resolveSymlinks(path, mode)};
    }
};

std::ostream & operator << (std::ostream & str, const SourcePath & path);

}
