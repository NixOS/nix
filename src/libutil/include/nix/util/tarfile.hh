#pragma once
///@file

#include "nix/util/canon-path.hh"
#include "nix/util/serialise.hh"
#include <archive.h>

namespace nix {

struct TarArchive
{
    struct archive * archive;
    Source * source;
    std::vector<unsigned char> buffer;

    void check(int err, const std::string & reason = "failed to extract archive (%s)");

    explicit TarArchive(const std::filesystem::path & path);

    /// @brief Create a generic archive from source.
    /// @param source - Input byte stream.
    /// @param raw - Whether to enable raw file support. For more info look in docs:
    /// https://manpages.debian.org/stretch/libarchive-dev/archive_read_format.3.en.html
    /// @param compression_method - Primary compression method to use. std::nullopt means 'all'.
    TarArchive(Source & source, bool raw = false, std::optional<std::string> compression_method = std::nullopt);

    /// Disable copy constructor. Explicitly default move assignment/constructor.
    TarArchive(const TarArchive &) = delete;
    TarArchive & operator=(const TarArchive &) = delete;
    TarArchive(TarArchive &&) = default;
    TarArchive & operator=(TarArchive &&) = default;

    void close();

    ~TarArchive();
};

int getArchiveFilterCodeByName(const std::string & method);

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
    virtual ~TarSink() = default;

    virtual void createDirectory(const CanonPath & path) = 0;

    virtual void createRegularFile(const CanonPath & path, bool isExecutable, fun<void(Sink &)> callback) = 0;

    virtual void createSymlink(const CanonPath & path, const std::string & target) = 0;

    /**
     * Create a hard link. The target must be the path of a previously
     * encountered file relative to the root.
     */
    virtual void createHardlink(const CanonPath & path, const CanonPath & target) = 0;
};

time_t unpackTarfileToSink(TarArchive & archive, TarSink & parseSink);

} // namespace nix
