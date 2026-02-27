#pragma once
///@file

#include "nix/util/file-descriptor.hh"
#include "nix/util/file-system.hh"
#include "nix/util/variant-wrapper.hh"

#include <filesystem>
#include <variant>

namespace nix {

/**
 * Where the root of a `RestoreSink` is located.
 *
 * - `DescriptorDestination::Parent`: Root doesn't exist yet; will be
 *   created with the given name in the parent directory. After root
 *   directory creation, this transitions to `AutoCloseFD`. This is the
 *   only way to allow the root to be something other than a directory.
 *
 * - `AutoCloseFD`: Root directory is already open, and must be a
 *   directory. This is the only way to restore directly to a root of a
 *   file system / drive, or some other directory that doesn't have a
 *   parent.
 */
struct DescriptorDestination
{
    /**
     * Parent directory and name for root entry.
     * Used when the root doesn't exist yet.
     */
    struct Parent
    {
        /**
         * Directory descriptor for the parent directory.
         */
        AutoCloseFD fd;

        /**
         * Must be a single filename, with no directory separators. Must
         * not be `.` `..` or some other special path that will not
         * point to an actual child of `fd`.
         */
        std::filesystem::path name;
    };

    using Raw = std::variant<Parent, AutoCloseFD>;

    Raw raw;

    MAKE_WRAPPER_CONSTRUCTOR_MOVE_ONLY(DescriptorDestination);

    /**
     * Create a `DescriptorDestination` from a given path.
     *
     * If the path has no parent (e.g., "/" or "C:\"), it must be a
     * directory and is opened directly as such, returning an
     * `AutoCloseFD`.
     *
     * Otherwise, the parent directory is opened and the filename is
     * extracted, returning a `Parent`.
     *
     * @param finalSymlink Effects whether we can get away with the
     * lexical parent (easy) or need the actual parent) harder. In the
     * actual parent case, we need to keep recurring until we've
     * followed all symlinks and get the true parent of the final
     * non-symlink.
     */
    static DescriptorDestination open(const std::filesystem::path & path, FinalSymlink finalSymlink);

    /**
     * Create a `DescriptorDestination` relative to a directory descriptor.
     *
     * If `path` is empty, we just return `dirFD` --- it better actually
     * be referring to directory!
     *
     * If `path` is a single path component and `finalSymlink =
     * NoFollow`, we just return `Parent` of the two, easy enough.
     *
     * If `path` is more than one path components and `finalSymlink =
     * NoFollow`, we can just take the parent lexically, open a new
     * directory descriptor from that, and return `Parent` with that and
     * the base name.
     *
     * If `finalSymlink = Follow` we have to do much the same work as
     * `open` to get the real parent.
     *
     * @pre `path` must be relative (not absolute).
     */
    static DescriptorDestination
    openAt(Descriptor dirFd, const std::filesystem::path & path, FinalSymlink finalSymlink);

    /**
     * Get the original path.
     *
     * @note Like the underlying `descriptorToPath`, this must be used
     * *just* for error messages.
     */
    std::filesystem::path toPath() const;
};

} // namespace nix
