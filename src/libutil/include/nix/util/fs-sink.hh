#pragma once
///@file

#include "nix/util/serialise.hh"
#include "nix/util/source-accessor.hh"
#include "nix/util/file-system.hh"

namespace nix {

/**
 * Actions on an open regular file in the process of creating it.
 *
 * See `FileSystemObjectSink::createRegularFile`.
 */
struct CreateRegularFileSink : Sink
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
 */
struct RestoreSink : FileSystemObjectSink
{
    std::filesystem::path dstPath;
    bool startFsync = false;

    explicit RestoreSink(bool startFsync)
        : startFsync{startFsync}
    {
    }

    void createDirectory(const CanonPath & path) override;

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
