#pragma once
///@file

#include "types.hh"
#include "serialise.hh"
#include "source-accessor.hh"
#include "file-system.hh"

namespace nix {

/**
 * Actions on an open regular file in the process of creating it.
 *
 * See `FileSystemObjectSink::createRegularFile`.
 */
struct CreateRegularFileSink : Sink
{
    virtual void isExecutable() = 0;

    /**
     * An optimization. By default, do nothing.
     */
    virtual void preallocateContents(uint64_t size) { };
};


struct FileSystemObjectSink
{
    virtual ~FileSystemObjectSink() = default;

    virtual void createDirectory(const Path & path) = 0;

    /**
     * This function in general is no re-entrant. Only one file can be
     * written at a time.
     */
    virtual void createRegularFile(
        const Path & path,
        std::function<void(CreateRegularFileSink &)>) = 0;

    virtual void createSymlink(const Path & path, const std::string & target) = 0;
};

/**
 * Recursively copy file system objects from the source into the sink.
 */
void copyRecursive(
    SourceAccessor & accessor, const CanonPath & sourcePath,
    FileSystemObjectSink & sink, const Path & destPath);

/**
 * Ignore everything and do nothing
 */
struct NullFileSystemObjectSink : FileSystemObjectSink
{
    void createDirectory(const Path & path) override { }
    void createSymlink(const Path & path, const std::string & target) override { }
    void createRegularFile(
        const Path & path,
        std::function<void(CreateRegularFileSink &)>) override;
};

/**
 * Write files at the given path
 */
struct RestoreSink : FileSystemObjectSink
{
    Path dstPath;

    void createDirectory(const Path & path) override;

    void createRegularFile(
        const Path & path,
        std::function<void(CreateRegularFileSink &)>) override;

    void createSymlink(const Path & path, const std::string & target) override;
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

    RegularFileSink(Sink & sink) : sink(sink) { }

    void createDirectory(const Path & path) override
    {
        regular = false;
    }

    void createSymlink(const Path & path, const std::string & target) override
    {
        regular = false;
    }

    void createRegularFile(
        const Path & path,
        std::function<void(CreateRegularFileSink &)>) override;
};

}
