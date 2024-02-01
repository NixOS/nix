#pragma once
///@file

#include "source-accessor.hh"
#include "fs-sink.hh"
#include "util.hh"

namespace nix {

/**
 * An enumeration of the main ways we can serialize file system
 * objects.
 */
enum struct FileIngestionMethod : uint8_t {
    /**
     * Flat-file hashing. Directly ingest the contents of a single file
     */
    Flat = 0,
    /**
     * Recursive (or NAR) hashing. Serializes the file-system object in
     * Nix Archive format and ingest that.
     */
    Recursive = 1,
};

/**
 * Dump a serialization of the given file system object.
 */
void dumpPath(
    SourceAccessor & accessor, const CanonPath & path,
    Sink & sink,
    FileIngestionMethod method,
    PathFilter & filter = defaultPathFilter);

/**
 * Restore a serialization of the given file system object.
 *
 * @TODO use an arbitrary `FileSystemObjectSink`.
 */
void restorePath(
    const Path & path,
    Source & source,
    FileIngestionMethod method);

/**
 * Compute the hash of the given file system object according to the
 * given method.
 *
 * The hash is defined as (essentially) hashString(ht, dumpPath(path)).
 */
HashResult hashPath(
    SourceAccessor & accessor, const CanonPath & path,
    FileIngestionMethod method, HashAlgorithm ht,
    PathFilter & filter = defaultPathFilter);

}
