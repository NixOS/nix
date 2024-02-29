#pragma once
///@file

#include "serialise.hh"
#include "fs-sink.hh"
#include <archive.h>

namespace nix {

struct TarArchive {
    struct archive * archive;
    Source * source;
    std::vector<unsigned char> buffer;

    void check(int err, const std::string & reason = "failed to extract archive (%s)");

    TarArchive(Source & source, bool raw = false);

    TarArchive(const Path & path);

    /// disable copy constructor
    TarArchive(const TarArchive &) = delete;

    void close();

    ~TarArchive();
};

void unpackTarfile(Source & source, const Path & destDir);

void unpackTarfile(const Path & tarFile, const Path & destDir);

time_t unpackTarfileToSink(TarArchive & archive, FileSystemObjectSink & parseSink);

}
