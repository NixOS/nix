#pragma once
///@file

#include "source-accessor.hh"
#include "fs-sink.hh"
#include "util.hh"

namespace nix {

/**
 * An enumeration of the ways we can serialize file system
 * objects.
 */
enum struct FileSerialisationMethod : uint8_t {
    /**
     * Flat-file. The contents of a single file exactly.
     */
    Flat,

    /**
     * Nix Archive. Serializes the file-system object in
     * Nix Archive format.
     */
    Recursive,
};

/**
 * Parse a `FileSerialisationMethod` by name. Choice of:
 *
 *  - `flat`: `FileSerialisationMethod::Flat`
 *  - `nar`: `FileSerialisationMethod::Recursive`
 *
 * Opposite of `renderFileSerialisationMethod`.
 */
FileSerialisationMethod parseFileSerialisationMethod(std::string_view input);

/**
 * Render a `FileSerialisationMethod` by name.
 *
 * Opposite of `parseFileSerialisationMethod`.
 */
std::string_view renderFileSerialisationMethod(FileSerialisationMethod method);

/**
 * Dump a serialization of the given file system object.
 */
void dumpPath(
    SourceAccessor & accessor, const CanonPath & path,
    Sink & sink,
    FileSerialisationMethod method,
    PathFilter & filter = defaultPathFilter);

/**
 * Restore a serialisation of the given file system object.
 *
 * @TODO use an arbitrary `FileSystemObjectSink`.
 */
void restorePath(
    const Path & path,
    Source & source,
    FileSerialisationMethod method);


/**
 * Compute the hash of the given file system object according to the
 * given method.
 *
 * the hash is defined as (in pseudocode):
 *
 * ```
 * hashString(ha, dumpPath(...))
 * ```
 */
HashResult hashPath(
    SourceAccessor & accessor, const CanonPath & path,
    FileSerialisationMethod method, HashAlgorithm ha,
    PathFilter & filter = defaultPathFilter);

/**
 * An enumeration of the ways we can ingest file system
 * objects, producing a hash or digest.
 */
enum struct FileIngestionMethod : uint8_t {
    /**
     * Hash `FileSerialisationMethod::Flat` serialisation.
     */
    Flat,

    /**
     * Hash `FileSerialisationMethod::Git` serialisation.
     */
    Recursive,

    /**
     * Git hashing. In particular files are hashed as git "blobs", and
     * directories are hashed as git "trees".
     *
     * Unlike `Flat` and `Recursive`, this is not a hash of a single
     * serialisation but a [Merkle
     * DAG](https://en.wikipedia.org/wiki/Merkle_tree) of multiple
     * rounds of serialisation and hashing.
     *
     * @note Git's data model is slightly different, in that a plain
     * file doesn't have an executable bit, directory entries do
     * instead. We decide treat a bare file as non-executable by fiat,
     * as we do with `FileIngestionMethod::Flat` which also lacks this
     * information. Thus, Git can encode some but all of Nix's "File
     * System Objects", and this sort of hashing is likewise partial.
     */
    Git,
};

/**
 * Parse a `FileIngestionMethod` by name. Choice of:
 *
 *  - `flat`: `FileIngestionMethod::Flat`
 *  - `nar`: `FileIngestionMethod::Recursive`
 *  - `git`: `FileIngestionMethod::Git`
 *
 * Opposite of `renderFileIngestionMethod`.
 */
FileIngestionMethod parseFileIngestionMethod(std::string_view input);

/**
 * Render a `FileIngestionMethod` by name.
 *
 * Opposite of `parseFileIngestionMethod`.
 */
std::string_view renderFileIngestionMethod(FileIngestionMethod method);

/**
 * Compute the hash of the given file system object according to the
 * given method.
 *
 * Unlike the other `hashPath`, this works on an arbitrary
 * `FileIngestionMethod` instead of `FileSerialisationMethod`, but
 * doesn't return the size as this is this is not a both simple and
 * useful defined for a merkle format.
 */
Hash hashPath(
    SourceAccessor & accessor, const CanonPath & path,
    FileIngestionMethod method, HashAlgorithm ha,
    PathFilter & filter = defaultPathFilter);

}
