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
#include "nix/util/merkle-files.hh"

namespace nix::git {

enum struct ObjectType {
    Blob,
    Tree,
    // Commit,
    // Tag,
};

// Backwards compatibility aliases
using merkle::Mode;
using merkle::TreeEntry;
using nix::RawMode;

/**
 * A Git tree object, fully decoded and stored in memory.
 *
 * Directory names must end in a `/` for sake of sorting. See
 * https://github.com/mirage/irmin/issues/352
 */
using Tree = std::map<std::string, TreeEntry>;

inline std::optional<Mode> decodeMode(RawMode m)
{
    return merkle::decodeMode(m);
}

/**
 * Parse the "blob " or "tree " prefix.
 *
 * @throws if prefix not recognized
 */
ObjectType
parseObjectType(Source & source, const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * Read the size of the blob
 *
 * The caller should then call `Source::drainInto` or similar with that
 * size.
 */
uint64_t parseBlob(Source & source, const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * @param hashAlgo must be `HashAlgo::SHA1` or `HashAlgo::SHA256` for now.
 */
void parseTree(
    merkle::DirectorySink & sink,
    Source & source,
    HashAlgorithm hashAlgo,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * Convert a `SourceAccessor::Type` to a `Mode`.
 */
std::optional<Mode> convertMode(SourceAccessor::Type type);

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
    fun<DumpHook> hook,
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
