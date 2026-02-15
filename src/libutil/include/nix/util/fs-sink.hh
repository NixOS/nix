#pragma once
///@file

#include "nix/util/file-descriptor.hh"
#include "nix/util/serialise.hh"
#include "nix/util/source-accessor.hh"

namespace nix {

/**
 * A way to directly and recursively sink file system objects
 *
 * This is a very well-restricted interface to recursively create file
 * system objects "bottom up", and "one layer at a time". For example of
 * some rules this enforces:
 *
 *  - Parent directories must be created before children.
 *
 *  - Child names must be picked before child contents (including file
 *    type).
 *
 *  - Metadata like the executable bit on files must be chosen before
 *    file concepts are streamed.
 *
 *  As such, this interface is very good for two tasks in particular:
 *
 *  - Parsing Nix Archives, as the NAR format exactly corresponds to
 *    this. (You can think of this as a NAR Visitor.)
 *
 *  - Creating files on disk, as the above invariants give us the
 *    opportunity to create files in a very controlled manner, avoiding
 *    all sorts of security issues.
 *
 *  What this is *not* good for is:
 *
 *  - Unpacking unstructured file formats like tarballs. (See `TarSink`
 *    for that.)
 *
 *  - Merkle formats/protocols that do not want to consume all layers at
 *    once, but support more asynchronous and lazy IO. (See
 *    `merkle::FileSinkBuilder` for that.)
 */
struct FileSystemObjectSink
{

    /**
     * Actions on an open regular file in the process of creating it.
     *
     * See `FileSystemObjectSink::createRegularFile`.
     */
    struct OnRegularFile : virtual Sink
    {
        /**
         * If set to true, the sink will not be called with the contents
         * of the file. `preallocateContents()` will still be called to
         * convey the file size. Useful for sinks that want to efficiently
         * discard the contents of the file.
         */
        bool skipContents = false;

        /**
         * An optimization. By default, do nothing.
         */
        virtual void preallocateContents(uint64_t size) {};
    };

    /**
     * Actions on an open directory in order to populate it with its
     * children.
     */
    struct OnDirectory
    {
        virtual ~OnDirectory() = default;

        using ChildCreatedCallback = fun<void(FileSystemObjectSink & fsoSink)>;

        /**
         * Create a child of this directory. The callback provides us with a
         * `FileSystemObjectSink` we can think of as "pointing" to the
         * location which we are to create something in, giving us our
         * choices.
         *
         * @param fileName must not contain any `/` or null bytes. It should
         * also not contain any `\` when Windows host filesystems aim to be
         * supported.
         */
        virtual void createChild(std::string_view fileName, ChildCreatedCallback callback) = 0;
    };

    virtual ~FileSystemObjectSink() = default;

    using DirectoryCreatedCallback = fun<void(OnDirectory & dirSink)>;

    /**
     * Create a directory and invoke a callback with a pair of sink + CanonPath
     * of the created subdirectory relative to dirSink.
     *
     * @note This allows for `RestoreSink` to implement
     * *at-style accessors that always keep an open file descriptor for the
     * freshly created directory. Use this when it's important to disallow any
     * intermediate path components from being symlinks.
     */
    virtual void createDirectory(DirectoryCreatedCallback callback) = 0;

    using RegularFileCreatedCallback = fun<void(OnRegularFile & regFileSink)>;

    /**
     * This function in general is no re-entrant. Only one file can be
     * written at a time.
     *
     * @param isExecutable whether the file should be marked executable
     */
    virtual void createRegularFile(bool isExecutable, RegularFileCreatedCallback callback) = 0;

    virtual void createSymlink(const std::string & target) = 0;
};

/**
 * Recursively copy file system objects from the source into the sink.
 */
void copyRecursive(SourceAccessor & accessor, const CanonPath & sourcePath, FileSystemObjectSink & sink);

/**
 * Ignore everything and do nothing
 */
struct NullFileSystemObjectSink : FileSystemObjectSink
{
    void createDirectory(DirectoryCreatedCallback) override;

    void createSymlink(const std::string & target) override {}

    void createRegularFile(bool isExecutable, RegularFileCreatedCallback) override;
};

/**
 * Write files at the given path
 *
 * This sink must *never* follow intermediate symlinks in case a file collision
 * is encountered for various reasons like case-insensitivity or other types of
 * normalization. Using appropriate *at system calls and traversing only one
 * path component at a time ensures that writing is race-free and is not
 * susceptible to symlink replacement.
 */
struct RestoreSink : FileSystemObjectSink
{
    /**
     * This is a borrowed descriptor; the caller must ensure it remains valid
     * for the lifetime of operations on this sink.
     */
    Descriptor parentDir;
    /**
     * Name of the child to create within the parent directory.
     */
    std::filesystem::path name;
    bool startFsync = false;

    /**
     * Implementation of OnDirectory for RestoreSink.
     */
    struct Directory : OnDirectory
    {
        AutoCloseFD directory;
        bool startFsync;

        Directory(AutoCloseFD directory, bool startFsync)
            : directory{std::move(directory)}
            , startFsync{startFsync}
        {
        }

        void createChild(std::string_view name, ChildCreatedCallback callback) override;
    };

    /**
     * Construct a sink.
     */
    explicit RestoreSink(Descriptor parentDir, std::filesystem::path name, bool startFsync = false)
        : parentDir{parentDir}
        , name{std::move(name)}
        , startFsync{startFsync}
    {
    }

    void createDirectory(DirectoryCreatedCallback callback) override;

    void createRegularFile(bool isExecutable, RegularFileCreatedCallback callback) override;

    void createSymlink(const std::string & target) override;
};

/**
 * Restore a single file at the top level, passing along
 * `receiveContents` to the underlying `Sink`. For anything but a single
 * file, set `regular = true` so the caller can fail accordingly.
 */
struct RegularFileSink : FileSystemObjectSink
{
    bool regular = true;
    Sink & sink;

    RegularFileSink(Sink & sink)
        : sink(sink)
    {
    }

    void createDirectory(DirectoryCreatedCallback) override
    {
        regular = false;
    }

    void createSymlink(const std::string & target) override
    {
        regular = false;
    }

    void createRegularFile(bool isExecutable, RegularFileCreatedCallback callback) override;
};

} // namespace nix
