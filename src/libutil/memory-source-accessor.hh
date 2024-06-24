#include "source-path.hh"
#include "fs-sink.hh"
#include "variant-wrapper.hh"

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
    struct File {
        bool operator == (const File &) const noexcept;
        std::strong_ordering operator <=> (const File &) const noexcept;

        struct Regular {
            bool executable = false;
            std::string contents;

            bool operator == (const Regular &) const = default;
            auto operator <=> (const Regular &) const = default;
        };

        struct Directory {
            using Name = std::string;

            std::map<Name, File, std::less<>> contents;

            bool operator == (const Directory &) const noexcept;
            auto operator <=> (const Directory &) const noexcept;
        };

        struct Symlink {
            std::string target;

            bool operator == (const Symlink &) const = default;
            auto operator <=> (const Symlink &) const = default;
        };

        using Raw = std::variant<Regular, Directory, Symlink>;
        Raw raw;

        MAKE_WRAPPER_CONSTRUCTOR(File);

        Stat lstat() const;
    };

    File root { File::Directory {} };

    bool operator == (const MemorySourceAccessor &) const noexcept = default;
    auto operator <=> (const MemorySourceAccessor &) const noexcept = default;

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


bool MemorySourceAccessor::File::Directory::operator == (
    const MemorySourceAccessor::File::Directory &) const noexcept = default;
auto MemorySourceAccessor::File::Directory::operator <=> (
    const MemorySourceAccessor::File::Directory &) const noexcept = default;

bool MemorySourceAccessor::File::operator == (
    const MemorySourceAccessor::File &) const noexcept = default;
std::strong_ordering MemorySourceAccessor::File::operator <=> (
    const MemorySourceAccessor::File &) const noexcept = default;

/**
 * Write to a `MemorySourceAccessor` at the given path
 */
struct MemorySink : FileSystemObjectSink
{
    MemorySourceAccessor & dst;

    MemorySink(MemorySourceAccessor & dst) : dst(dst) { }

    void createDirectory(const Path & path) override;

    void createRegularFile(
        const Path & path,
        std::function<void(CreateRegularFileSink &)>) override;

    void createSymlink(const Path & path, const std::string & target) override;
};

}
