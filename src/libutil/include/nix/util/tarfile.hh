#pragma once
///@file

#include "nix/util/serialise.hh"
#include "nix/util/fs-sink.hh"
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

void unpackTarfile(Source & source, const std::filesystem::path & destDir);

void unpackTarfile(const std::filesystem::path & tarFile, const std::filesystem::path & destDir);

time_t unpackTarfileToSink(TarArchive & archive, ExtendedFileSystemObjectSink & parseSink);

} // namespace nix
