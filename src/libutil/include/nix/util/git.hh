#pragma once
///@file

#include <string>
#include <string_view>
#include <optional>

#include "nix/util/types.hh"
#include "nix/util/serialise.hh"
#include "nix/util/hash.hh"
#include "nix/util/source-path.hh"
#include "nix/util/fs-sink.hh"

namespace nix::git {

enum struct ObjectType {
    Blob,
    Tree,
    // Commit,
    // Tag,
};

using RawMode = uint32_t;

enum struct Mode : RawMode {
    Directory = 0040000,
    Regular = 0100644,
    Executable = 0100755,
    Symlink = 0120000,
};

std::optional<Mode> decodeMode(RawMode m);

/**
 * An anonymous Git tree object entry (no name part).
 */
struct TreeEntry
{
    Mode mode;
    Hash hash;

    bool operator==(const TreeEntry &) const = default;
    auto operator<=>(const TreeEntry &) const = default;
};

/**
 * A Git tree object, fully decoded and stored in memory.
 *
 * Directory names must end in a `/` for sake of sorting. See
 * https://github.com/mirage/irmin/issues/352
 */
using Tree = std::map<std::string, TreeEntry>;

/**
 * Callback for processing a child hash with `parse`
 *
 * The function should
 *
 * 1. Obtain the file system objects denoted by `gitHash`
 *
 * 2. Ensure they match `mode`
 *
 * 3. Feed them into the same sink `parse` was called with
 *
 * Implementations may seek to memoize resources (bandwidth, storage,
 * etc.) for the same Git hash.
 */
using SinkHook = void(const CanonPath & name, TreeEntry entry);

/**
 * Parse the "blob " or "tree " prefix.
 *
 * @throws if prefix not recognized
 */
ObjectType
parseObjectType(Source & source, const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * These 3 modes are represented by blob objects.
 *
 * Sometimes we need this information to disambiguate how a blob is
 * being used to better match our own "file system object" data model.
 */
enum struct BlobMode : RawMode {
    Regular = static_cast<RawMode>(Mode::Regular),
    Executable = static_cast<RawMode>(Mode::Executable),
    Symlink = static_cast<RawMode>(Mode::Symlink),
};

void parseBlob(
    FileSystemObjectSink & sink,
    const CanonPath & sinkPath,
    Source & source,
    BlobMode blobMode,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * @param hashAlgo must be `HashAlgo::SHA1` or `HashAlgo::SHA256` for now.
 */
void parseTree(
    FileSystemObjectSink & sink,
    const CanonPath & sinkPath,
    Source & source,
    HashAlgorithm hashAlgo,
    std::function<SinkHook> hook,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * Helper putting the previous three `parse*` functions together.
 *
 * @param rootModeIfBlob How to interpret a root blob, for which there is no
 * disambiguating dir entry to answer that questino. If the root it not
 * a blob, this is ignored.
 *
 * @param hashAlgo must be `HashAlgo::SHA1` or `HashAlgo::SHA256` for now.
 */
void parse(
    FileSystemObjectSink & sink,
    const CanonPath & sinkPath,
    Source & source,
    BlobMode rootModeIfBlob,
    HashAlgorithm hashAlgo,
    std::function<SinkHook> hook,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * Assists with writing a `SinkHook` step (2).
 */
std::optional<Mode> convertMode(SourceAccessor::Type type);

/**
 * Simplified version of `SinkHook` for `restore`.
 *
 * Given a `Hash`, return a `SourceAccessor` and `CanonPath` pointing to
 * the file system object with that path.
 */
using RestoreHook = SourcePath(Hash);

/**
 * Wrapper around `parse` and `RestoreSink`
 *
 * @param hashAlgo must be `HashAlgo::SHA1` or `HashAlgo::SHA256` for now.
 */
void restore(FileSystemObjectSink & sink, Source & source, HashAlgorithm hashAlgo, std::function<RestoreHook> hook);

/**
 * Dumps a single file to a sink
 *
 * @param xpSettings for testing purposes
 */
void dumpBlobPrefix(
    uint64_t size, Sink & sink, const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * Dumps a representation of a git tree to a sink
 */
void dumpTree(
    const Tree & entries, Sink & sink, const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * Callback for processing a child with `dump`
 *
 * The function should return the Git hash and mode of the file at the
 * given path in the accessor passed to `dump`.
 *
 * Note that if the child is a directory, its child in must also be so
 * processed in order to compute this information.
 */
using DumpHook = TreeEntry(const SourcePath & path);

Mode dump(
    const SourcePath & path,
    Sink & sink,
    std::function<DumpHook> hook,
    PathFilter & filter = defaultPathFilter,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * Recursively dumps path, hashing as we go.
 *
 * A smaller wrapper around `dump`.
 */
TreeEntry dumpHash(HashAlgorithm ha, const SourcePath & path, PathFilter & filter = defaultPathFilter);

/**
 * A line from the output of `git ls-remote --symref`.
 *
 * These can be of two kinds:
 *
 * - Symbolic references of the form
 *
 *   ```
 *   ref: {target} {reference}
 *   ```
 *   where {target} is itself a reference and {reference} is optional
 *
 * - Object references of the form
 *
 *   ```
 *   {target}  {reference}
 *   ```
 *   where {target} is a commit id and {reference} is mandatory
 */
struct LsRemoteRefLine
{
    enum struct Kind { Symbolic, Object };
    Kind kind;
    std::string target;
    std::optional<std::string> reference;
};

/**
 * Parse an `LsRemoteRefLine`
 */
std::optional<LsRemoteRefLine> parseLsRemoteLine(std::string_view line);

} // namespace nix::git
