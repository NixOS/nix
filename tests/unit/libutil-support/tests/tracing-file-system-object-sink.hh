#pragma once
#include "fs-sink.hh"

namespace nix::test {

/**
 * A `FileSystemObjectSink` that traces calls, writing to stderr.
 */
class TracingFileSystemObjectSink : public virtual FileSystemObjectSink
{
    FileSystemObjectSink & sink;
public:
    TracingFileSystemObjectSink(FileSystemObjectSink & sink)
        : sink(sink)
    {
    }

    void createDirectory(const Path & path) override;

    void createRegularFile(const Path & path, std::function<void(CreateRegularFileSink &)> fn);

    void createSymlink(const Path & path, const std::string & target);
};

/**
 * A `ExtendedFileSystemObjectSink` that traces calls, writing to stderr.
 */
class TracingExtendedFileSystemObjectSink : public TracingFileSystemObjectSink, public ExtendedFileSystemObjectSink
{
    ExtendedFileSystemObjectSink & sink;
public:
    TracingExtendedFileSystemObjectSink(ExtendedFileSystemObjectSink & sink)
        : TracingFileSystemObjectSink(sink)
        , sink(sink)
    {
    }

    void createHardlink(const Path & path, const CanonPath & target);
};

}
