#pragma once
///@file

#include "types.hh"
#include "serialise.hh"
#include "source-accessor.hh"
#include "file-system.hh"

namespace nix {

/**
 * \todo Fix this API, it sucks.
 */
struct ParseSink
{
    virtual void createDirectory(const Path & path) = 0;

    virtual void createRegularFile(const Path & path) = 0;
    virtual void receiveContents(std::string_view data) = 0;
    virtual void isExecutable() = 0;
    virtual void closeRegularFile() = 0;

    virtual void createSymlink(const Path & path, const std::string & target) = 0;

    /**
     * An optimization. By default, do nothing.
     */
    virtual void preallocateContents(uint64_t size) { };
};

/**
 * Recusively copy file system objects from the source into the sink.
 */
void copyRecursive(
    SourceAccessor & accessor, const CanonPath & sourcePath,
    ParseSink & sink, const Path & destPath);

/**
 * Ignore everything and do nothing
 */
struct NullParseSink : ParseSink
{
    void createDirectory(const Path & path) override { }
    void receiveContents(std::string_view data) override { }
    void createSymlink(const Path & path, const std::string & target) override { }
    void createRegularFile(const Path & path) override { }
    void closeRegularFile() override { }
    void isExecutable() override { }
};

/**
 * Write files at the given path
 */
struct RestoreSink : ParseSink
{
    Path dstPath;

    bool protect;

    void createDirectory(const Path & path) override;

    void createRegularFile(const Path & path) override;
    void receiveContents(std::string_view data) override;
    void isExecutable() override;
    void closeRegularFile() override;

    void createSymlink(const Path & path, const std::string & target) override;

    void preallocateContents(uint64_t size) override;

private:
    AutoCloseFD fd;
};

/**
 * Restore a single file at the top level, passing along
 * `receiveContents` to the underlying `Sink`. For anything but a single
 * file, set `regular = true` so the caller can fail accordingly.
 */
struct RegularFileSink : ParseSink
{
    bool regular = true;
    Sink & sink;

    RegularFileSink(Sink & sink) : sink(sink) { }

    void createDirectory(const Path & path) override
    {
        regular = false;
    }

    void receiveContents(std::string_view data) override
    {
        sink(data);
    }

    void createSymlink(const Path & path, const std::string & target) override
    {
        regular = false;
    }

    void createRegularFile(const Path & path) override { }
    void closeRegularFile() override { }
    void isExecutable() override { }
};

}
