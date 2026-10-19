#pragma once
///@file

#include "nix/util/compression-algo.hh"

#include <exception>

struct archive;

namespace nix {

struct Source;
struct Sink;
class CanonPath;

struct TarArchive
{
    ::archive * archive;
    Source * source;
    std::vector<unsigned char> buffer;
    /* Exception encountered when reading the source. */
    std::exception_ptr readException;

    void check(int err, const std::string & reason = "failed to extract archive (%s)", bool warningsAreFatal = true);

    explicit TarArchive(const std::filesystem::path & path);

    /**
     * @brief Create a generic archive from source.
     *
     * @param source Input byte stream.
     * @param raw Whether to enable raw file support. Used to decompress arbitrary streams.
     * @param compressionMethod Primary compression method to use. std::nullopt means 'all'.
     */
    TarArchive(Source & source, bool raw = false, std::optional<CompressionAlgo> compressionMethod = std::nullopt);
    TarArchive(const TarArchive &) = delete;
    TarArchive & operator=(const TarArchive &) = delete;
    TarArchive(TarArchive &&) = default;
    TarArchive & operator=(TarArchive &&) = default;
    ~TarArchive();

    void close();
};

void unpackTarfile(const std::filesystem::path & tarFile, const std::filesystem::path & destDir);

/**
 * Sink for unpacking archives. Uses a path-based interface since
 * archives can have entries in any order.
 *
 * Of the three file system object sinks we have currently (this,
 * `FileSystemObjectSink`, and `merkle::FileSinkBuilder`), this one is by far
 * the "messiest", imposing the least structure. It is this rather a
 * footgun, and one should avoid implementing it directly as much as
 * possible.
 */
struct TarSink
{
    virtual ~TarSink();

    virtual void createDirectory(const CanonPath & path) = 0;

    virtual void createRegularFile(const CanonPath & path, bool isExecutable, fun<void(Sink &)> callback) = 0;

    virtual void createSymlink(const CanonPath & path, const std::string & target) = 0;

    /**
     * Create a hard link. The target must be the path of a previously
     * encountered file relative to the root.
     */
    virtual void createHardlink(const CanonPath & path, const CanonPath & target) = 0;
};

/**
 * A TarSink which just ignores all writes.
 */
struct NullTarSink : TarSink
{
    NullTarSink() {}

    ~NullTarSink();

    void createDirectory(const CanonPath & path) override {}

    void createRegularFile(const CanonPath & path, bool isExecutable, fun<void(Sink &)> callback) override;

    void createSymlink(const CanonPath & path, const std::string & target) override {}

    void createHardlink(const CanonPath & path, const CanonPath & target) override {}
};

time_t unpackTarfileToSink(TarArchive & archive, TarSink & parseSink);

} // namespace nix
