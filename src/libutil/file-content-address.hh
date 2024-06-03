#pragma once
///@file

#include "source-accessor.hh"
#include "fs-sink.hh"
#include "util.hh"

namespace nix {

struct SourcePath;

/**
 * An enumeration of the ways we can serialize file system
 * objects.
 *
 * See `file-system-object/content-address.md#serial` in the manual for
 * a user-facing description of this concept, but note that this type is also
 * used for storing or sending copies; not just for addressing.
 * Note also that there are other content addressing methods that don't
 * correspond to a serialisation method.
 */
enum struct FileSerialisationMethod : uint8_t {
    /**
     * Flat-file. The contents of a single file exactly.
     *
     * See `file-system-object/content-address.md#serial-flat` in the
     * manual.
     */
    Flat,

    /**
     * Nix Archive. Serializes the file-system object in
     * Nix Archive format.
     *
     * See `file-system-object/content-address.md#serial-nix-archive` in
     * the manual.
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
    const SourcePath & path,
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
    const SourcePath & path,
    FileSerialisationMethod method, HashAlgorithm ha,
    PathFilter & filter = defaultPathFilter);

/**
 * An enumeration of the ways we can ingest file system
 * objects, producing a hash or digest.
 *
 * See `file-system-object/content-address.md` in the manual for a
 * user-facing description of this concept.
 */
enum struct FileIngestionMethod : uint8_t {
    /**
     * Hash `FileSerialisationMethod::Flat` serialisation.
     *
     * See `file-system-object/content-address.md#serial-flat` in the
     * manual.
     */
    Flat,

    /**
     * Hash `FileSerialisationMethod::Recursive` serialisation.
     *
     * See `file-system-object/content-address.md#serial-flat` in the
     * manual.
     */
    Recursive,

    /**
     * Git hashing.
     *
     * See `file-system-object/content-address.md#serial-git` in the
     * manual.
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
 * given method, and for some ingestion methods, the size of the
 * serialisation.
 *
 * Unlike the other `hashPath`, this works on an arbitrary
 * `FileIngestionMethod` instead of `FileSerialisationMethod`, but
 * may not return the size as this is this is not a both simple and
 * useful defined for a merkle format.
 */
std::pair<Hash, std::optional<uint64_t>> hashPath(
    const SourcePath & path,
    FileIngestionMethod method, HashAlgorithm ha,
    PathFilter & filter = defaultPathFilter);

}
