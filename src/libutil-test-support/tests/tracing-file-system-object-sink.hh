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

    void createDirectory(const CanonPath & path) override;

    void createRegularFile(const CanonPath & path, std::function<void(CreateRegularFileSink &)> fn) override;

    void createSymlink(const CanonPath & path, const std::string & target) override;
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

    void createHardlink(const CanonPath & path, const CanonPath & target) override;
};

}
