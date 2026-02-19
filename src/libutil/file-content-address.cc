#include "nix/util/file-content-address.hh"
#include "nix/util/archive.hh"
#include "nix/util/git.hh"
#include "nix/util/source-path.hh"

namespace nix {

static std::optional<FileSerialisationMethod> parseFileSerialisationMethodOpt(std::string_view input)
{
    if (input == "flat") {
        return FileSerialisationMethod::Flat;
    } else if (input == "nar") {
        return FileSerialisationMethod::NixArchive;
    } else {
        return std::nullopt;
    }
}

FileSerialisationMethod parseFileSerialisationMethod(std::string_view input)
{
    auto ret = parseFileSerialisationMethodOpt(input);
    if (ret)
        return *ret;
    else
        throw UsageError("Unknown file serialiation method '%s', expect `flat` or `nar`", input);
}

FileIngestionMethod parseFileIngestionMethod(std::string_view input)
{
    if (input == "git") {
        return FileIngestionMethod::Git;
    } else {
        auto ret = parseFileSerialisationMethodOpt(input);
        if (ret)
            return static_cast<FileIngestionMethod>(*ret);
        else
            throw UsageError("Unknown file ingestion method '%s', expect `flat`, `nar`, or `git`", input);
    }
}

std::string_view renderFileSerialisationMethod(FileSerialisationMethod method)
{
    switch (method) {
    case FileSerialisationMethod::Flat:
        return "flat";
    case FileSerialisationMethod::NixArchive:
        return "nar";
    default:
        assert(false);
    }
}

std::string_view renderFileIngestionMethod(FileIngestionMethod method)
{
    switch (method) {
    case FileIngestionMethod::Flat:
    case FileIngestionMethod::NixArchive:
        return renderFileSerialisationMethod(static_cast<FileSerialisationMethod>(method));
    case FileIngestionMethod::Git:
        return "git";
    default:
        unreachable();
    }
}

void dumpPath(const SourcePath & path, Sink & sink, FileSerialisationMethod method, PathFilter & filter)
{
    switch (method) {
    case FileSerialisationMethod::Flat:
        path.readFile(sink);
        break;
    case FileSerialisationMethod::NixArchive:
        path.dumpPath(sink, filter);
        break;
    }
}

void restorePath(const std::filesystem::path & path, Source & source, FileSerialisationMethod method, bool startFsync)
{
    switch (method) {
    case FileSerialisationMethod::Flat:
        writeFile(path, source, 0666, startFsync ? FsSync::Yes : FsSync::No);
        break;
    case FileSerialisationMethod::NixArchive:
        restorePath(path, source, startFsync);
        break;
    }
}

HashResult hashPath(const SourcePath & path, FileSerialisationMethod method, HashAlgorithm ha, PathFilter & filter)
{
    HashSink sink{ha};
    dumpPath(path, sink, method, filter);
    return sink.finish();
}

std::pair<Hash, std::optional<uint64_t>>
hashPath(const SourcePath & path, FileIngestionMethod method, HashAlgorithm ht, PathFilter & filter)
{
    switch (method) {
    case FileIngestionMethod::Flat:
    case FileIngestionMethod::NixArchive: {
        auto res = hashPath(path, (FileSerialisationMethod) method, ht, filter);
        return {res.hash, res.numBytesDigested};
    }
    case FileIngestionMethod::Git:
        return {git::dumpHash(ht, path, filter).hash, std::nullopt};
    }
    assert(false);
}

} // namespace nix
