#pragma once
///@file

#include <cstdint>
#include <optional>

#include "nix/util/hash.hh"
#include "nix/util/serialise.hh"

namespace nix {

using RawMode = uint32_t;

namespace merkle {

/**
 * The mode of a file in a content-addressed/merkle file system.
 * These values match the Git file modes.
 */
enum struct Mode : RawMode {
    Directory = 0040000,
    Regular = 0100644,
    Executable = 0100755,
    Symlink = 0120000,
};

inline std::optional<Mode> decodeMode(RawMode m)
{
    switch (m) {
    case (RawMode) Mode::Directory:
    case (RawMode) Mode::Executable:
    case (RawMode) Mode::Regular:
    case (RawMode) Mode::Symlink:
        return (Mode) m;
    default:
        return std::nullopt;
    }
}

/**
 * An entry in a content-addressed/merkle file system directory.
 */
struct TreeEntry
{
    Mode mode;
    Hash hash;

    bool operator==(const TreeEntry &) const = default;
    auto operator<=>(const TreeEntry &) const = default;
};

/**
 * A sink for building a shallow Merkle directory by inserting child
 * entries --- they contain a hash but no data.
 */
struct DirectorySink
{
    virtual ~DirectorySink() = default;

    /**
     * Insert an existing object as a child of this directory.
     */
    virtual void insertChild(std::string_view name, TreeEntry entry) = 0;
};

} // namespace merkle

} // namespace nix
