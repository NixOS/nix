#include "file-content-address.hh"
#include "archive.hh"

namespace nix {

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
