#include "file-content-address.hh"
#include "archive.hh"

namespace nix {

FileIngestionMethod parseFileIngestionMethod(std::string_view input)
{
    if (input == "flat") {
        return FileIngestionMethod::Flat;
    } else if (input == "nar") {
        return FileIngestionMethod::Recursive;
    } else {
        throw UsageError("Unknown file ingestion method '%s', expect `flat` or `nar`");
    }
}


std::string_view renderFileIngestionMethod(FileIngestionMethod method)
{
    switch (method) {
    case FileIngestionMethod::Flat:
        return "flat";
    case FileIngestionMethod::Recursive:
        return "nar";
    default:
        abort();
    }
}


void dumpPath(
    SourceAccessor & accessor, const CanonPath & path,
    Sink & sink,
    FileIngestionMethod method,
    PathFilter & filter)
{
    switch (method) {
    case FileIngestionMethod::Flat:
        accessor.readFile(path, sink);
        break;
    case FileIngestionMethod::Recursive:
        accessor.dumpPath(path, sink, filter);
        break;
    }
}


void restorePath(
    const Path & path,
    Source & source,
    FileIngestionMethod method)
{
    switch (method) {
    case FileIngestionMethod::Flat:
        writeFile(path, source);
        break;
    case FileIngestionMethod::Recursive:
        restorePath(path, source);
        break;
    }
}


HashResult hashPath(
    SourceAccessor & accessor, const CanonPath & path,
    FileIngestionMethod method, HashAlgorithm ht,
    PathFilter & filter)
{
    HashSink sink { ht };
    dumpPath(accessor, path, sink, method, filter);
    return sink.finish();
}

}
