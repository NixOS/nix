#pragma once
///@file

#include "serialise.hh"
#include "fs-sink.hh"
#include <archive.h>

namespace nix {

struct TarArchive
{
    struct archive * archive;
    Source * source;
    std::vector<unsigned char> buffer;

    void check(int err, const std::string & reason = "failed to extract archive (%s)");

    explicit TarArchive(const Path & path);

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

void unpackTarfile(Source & source, const Path & destDir);

void unpackTarfile(const Path & tarFile, const Path & destDir);

time_t unpackTarfileToSink(TarArchive & archive, FileSystemObjectSink & parseSink);

}
