#include "file-content-address.hh"
#include "archive.hh"
#include "git.hh"

namespace nix {

static std::optional<FileSerialisationMethod> parseFileSerialisationMethodOpt(std::string_view input)
{
    if (input == "flat") {
        return FileSerialisationMethod::Flat;
    } else if (input == "nar") {
        return FileSerialisationMethod::Recursive;
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
        throw UsageError("Unknown file serialiation method '%s', expect `flat` or `nar`");
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
            throw UsageError("Unknown file ingestion method '%s', expect `flat`, `nar`, or `git`");
    }
}


std::string_view renderFileSerialisationMethod(FileSerialisationMethod method)
{
    switch (method) {
    case FileSerialisationMethod::Flat:
        return "flat";
    case FileSerialisationMethod::Recursive:
        return "nar";
    default:
        assert(false);
    }
}


std::string_view renderFileIngestionMethod(FileIngestionMethod method)
{
    switch (method) {
    case FileIngestionMethod::Flat:
    case FileIngestionMethod::Recursive:
        return renderFileSerialisationMethod(
            static_cast<FileSerialisationMethod>(method));
    case FileIngestionMethod::Git:
        return "git";
    default:
        abort();
    }
}


void dumpPath(
    SourceAccessor & accessor, const CanonPath & path,
    Sink & sink,
    FileSerialisationMethod method,
    PathFilter & filter)
{
    switch (method) {
    case FileSerialisationMethod::Flat:
        accessor.readFile(path, sink);
        break;
    case FileSerialisationMethod::Recursive:
        accessor.dumpPath(path, sink, filter);
        break;
    }
}


void restorePath(
    const Path & path,
    Source & source,
    FileSerialisationMethod method)
{
    switch (method) {
    case FileSerialisationMethod::Flat:
        writeFile(path, source);
        break;
    case FileSerialisationMethod::Recursive:
        restorePath(path, source);
        break;
    }
}


HashResult hashPath(
    SourceAccessor & accessor, const CanonPath & path,
    FileSerialisationMethod method, HashAlgorithm ha,
    PathFilter & filter)
{
    HashSink sink { ha };
    dumpPath(accessor, path, sink, method, filter);
    return sink.finish();
}


Hash hashPath(
    SourceAccessor & accessor, const CanonPath & path,
    FileIngestionMethod method, HashAlgorithm ht,
    PathFilter & filter)
{
    switch (method) {
    case FileIngestionMethod::Flat:
    case FileIngestionMethod::Recursive:
        return hashPath(accessor, path, (FileSerialisationMethod) method, ht, filter).first;
    case FileIngestionMethod::Git:
        return git::dumpHash(ht, accessor, path, filter).hash;
    }
    assert(false);
}

}
