#pragma once
///@file

#include "nix/util/source-path.hh"
#include "nix/util/fs-sink.hh"
#include "nix/util/variant-wrapper.hh"

namespace nix {

/**
 * An source accessor for an in-memory file system.
 */
struct MemorySourceAccessor : virtual SourceAccessor
{
    /**
     * In addition to being part of the implementation of
     * `MemorySourceAccessor`, this has a side benefit of nicely
     * defining what a "file system object" is in Nix.
     */
    struct File
    {
        bool operator==(const File &) const noexcept;
        std::strong_ordering operator<=>(const File &) const noexcept;

        struct Regular
        {
            bool executable = false;
            std::string contents;

            bool operator==(const Regular &) const = default;
            auto operator<=>(const Regular &) const = default;
        };

        struct Directory
        {
            using Name = std::string;

            std::map<Name, File, std::less<>> contents;

            bool operator==(const Directory &) const noexcept;
            // TODO libc++ 16 (used by darwin) missing `std::map::operator <=>`, can't do yet.
            bool operator<(const Directory &) const noexcept;
        };

        struct Symlink
        {
            std::string target;

            bool operator==(const Symlink &) const = default;
            auto operator<=>(const Symlink &) const = default;
        };

        using Raw = std::variant<Regular, Directory, Symlink>;
        Raw raw;

        MAKE_WRAPPER_CONSTRUCTOR(File);

        Stat lstat() const;
    };

    std::optional<File> root;

    bool operator==(const MemorySourceAccessor &) const noexcept = default;

    bool operator<(const MemorySourceAccessor & other) const noexcept
    {
        return root < other.root;
    }

    std::string readFile(const CanonPath & path) override;
    bool pathExists(const CanonPath & path) override;
    std::optional<Stat> maybeLstat(const CanonPath & path) override;
    DirEntries readDirectory(const CanonPath & path) override;
    std::string readLink(const CanonPath & path) override;

    /**
     * @param create If present, create this file and any parent directories
     * that are needed.
     *
     * Return null if
     *
     * - `create = false`: File does not exist.
     *
     * - `create = true`: some parent file was not a dir, so couldn't
     *   look/create inside.
     */
    File * open(const CanonPath & path, std::optional<File> create);

    SourcePath addFile(CanonPath path, std::string && contents);
};

inline bool MemorySourceAccessor::File::Directory::operator==(
    const MemorySourceAccessor::File::Directory &) const noexcept = default;

inline bool
MemorySourceAccessor::File::Directory::operator<(const MemorySourceAccessor::File::Directory & other) const noexcept
{
    return contents < other.contents;
}

inline bool MemorySourceAccessor::File::operator==(const MemorySourceAccessor::File &) const noexcept = default;
inline std::strong_ordering
MemorySourceAccessor::File::operator<=>(const MemorySourceAccessor::File &) const noexcept = default;

/**
 * Write to a `MemorySourceAccessor` at the given path
 */
struct MemorySink : FileSystemObjectSink
{
    MemorySourceAccessor & dst;

    MemorySink(MemorySourceAccessor & dst)
        : dst(dst)
    {
    }

    void createDirectory(const CanonPath & path) override;

    void createRegularFile(const CanonPath & path, std::function<void(CreateRegularFileSink &)>) override;

    void createSymlink(const CanonPath & path, const std::string & target) override;
};

} // namespace nix
