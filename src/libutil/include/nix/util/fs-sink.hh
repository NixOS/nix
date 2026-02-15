#pragma once
///@file

#include "nix/util/descriptor-destination.hh"
#include "nix/util/serialise.hh"
#include "nix/util/source-accessor.hh"

namespace nix {

/**
 * Actions on an open regular file in the process of creating it.
 *
 * See `FileSystemObjectSink::createRegularFile`.
 */
struct CreateRegularFileSink : virtual Sink
{
    /**
     * If set to true, the sink will not be called with the contents
     * of the file. `preallocateContents()` will still be called to
     * convey the file size. Useful for sinks that want to efficiently
     * discard the contents of the file.
     */
    bool skipContents = false;

    virtual void isExecutable() = 0;

    /**
     * An optimization. By default, do nothing.
     */
    virtual void preallocateContents(uint64_t size) {};
};

struct FileSystemObjectSink
{
    virtual ~FileSystemObjectSink() = default;

    virtual void createDirectory(const CanonPath & path) = 0;

    using DirectoryCreatedCallback = std::function<void(FileSystemObjectSink & dirSink, const CanonPath & dirRelPath)>;

    /**
     * Create a directory and invoke a callback with a pair of sink + CanonPath
     * of the created subdirectory relative to dirSink.
     *
     * @note This allows for `RestoreSink` to implement
     * *at-style accessors that always keep an open file descriptor for the
     * freshly created directory. Use this when it's important to disallow any
     * intermediate path components from being symlinks.
     */
    virtual void createDirectory(const CanonPath & path, DirectoryCreatedCallback callback)
    {
        createDirectory(path);
        callback(*this, path);
    }

    /**
     * This function in general is no re-entrant. Only one file can be
     * written at a time.
     */
    virtual void createRegularFile(const CanonPath & path, std::function<void(CreateRegularFileSink &)>) = 0;

    virtual void createSymlink(const CanonPath & path, const std::string & target) = 0;
};

/**
 * An extension of `FileSystemObjectSink` that supports file types
 * that are not supported by Nix's FSO model.
 */
struct ExtendedFileSystemObjectSink : virtual FileSystemObjectSink
{
    /**
     * Create a hard link. The target must be the path of a previously
     * encountered file relative to the root of the FSO.
     */
    virtual void createHardlink(const CanonPath & path, const CanonPath & target) = 0;
};

/**
 * Recursively copy file system objects from the source into the sink.
 */
void copyRecursive(
    SourceAccessor & accessor, const CanonPath & sourcePath, FileSystemObjectSink & sink, const CanonPath & destPath);

/**
 * Ignore everything and do nothing
 */
struct NullFileSystemObjectSink : FileSystemObjectSink
{
    void createDirectory(const CanonPath & path) override {}

    void createSymlink(const CanonPath & path, const std::string & target) override {}

    void createRegularFile(const CanonPath & path, std::function<void(CreateRegularFileSink &)>) override;
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
    DescriptorDestination destination;

    bool startFsync = false;

    /**
     * Construct a sink.
     */
    explicit RestoreSink(DescriptorDestination dest, bool startFsync = false)
        : destination{std::move(dest)}
        , startFsync{startFsync}
    {
    }

    void createDirectory(const CanonPath & path) override;

    void createDirectory(const CanonPath & path, DirectoryCreatedCallback callback) override;

    void createRegularFile(const CanonPath & path, std::function<void(CreateRegularFileSink &)>) override;

    void createSymlink(const CanonPath & path, const std::string & target) override;
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

    void createDirectory(const CanonPath & path) override
    {
        regular = false;
    }

    void createSymlink(const CanonPath & path, const std::string & target) override
    {
        regular = false;
    }

    void createRegularFile(const CanonPath & path, std::function<void(CreateRegularFileSink &)>) override;
};

} // namespace nix
