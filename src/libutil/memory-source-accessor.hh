#include "source-accessor.hh"
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
        struct Regular {
            bool executable = false;
            std::string contents;

            GENERATE_CMP(Regular, me->executable, me->contents);
        };

        struct Directory {
            using Name = std::string;

            std::map<Name, File, std::less<>> contents;

            GENERATE_CMP(Directory, me->contents);
        };

        struct Symlink {
            std::string target;

            GENERATE_CMP(Symlink, me->target);
        };

        using Raw = std::variant<Regular, Directory, Symlink>;
        Raw raw;

        MAKE_WRAPPER_CONSTRUCTOR(File);

        GENERATE_CMP(File, me->raw);

        Stat lstat() const;
    };

    File root { File::Directory {} };

    GENERATE_CMP(MemorySourceAccessor, me->root);

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

    CanonPath addFile(CanonPath path, std::string && contents);
};

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
