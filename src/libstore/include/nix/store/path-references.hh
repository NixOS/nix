#pragma once
///@file

#include "nix/store/references.hh"
#include "nix/store/path.hh"
#include "nix/util/source-accessor.hh"

#include <functional>
#include <vector>

namespace nix {

StorePathSet scanForReferences(Sink & toTee, const Path & path, const StorePathSet & refs);

class PathRefScanSink : public RefScanSink
{
    std::map<std::string, StorePath> backMap;

    PathRefScanSink(StringSet && hashes, std::map<std::string, StorePath> && backMap);

public:

    static PathRefScanSink fromPaths(const StorePathSet & refs);

    StorePathSet getResultPaths();
};

/**
 * Result of scanning a single file for references.
 */
struct FileRefScanResult
{
    CanonPath filePath;     ///< The file that was scanned
    StorePathSet foundRefs; ///< Which store paths were found in this file
};

/**
 * Scan a store path tree and report which references appear in which files.
 *
 * This is like scanForReferences() but provides per-file granularity.
 * Useful for cycle detection and detailed dependency analysis like `nix why-depends --precise`.
 *
 * The function walks the tree using the provided accessor and streams each file's
 * contents through a RefScanSink to detect hash references. For each file that
 * contains at least one reference, a callback is invoked with the file path and
 * the set of references found.
 *
 * Note: This function only searches for the hash part of store paths (e.g.,
 * "dc04vv14dak1c1r48qa0m23vr9jy8sm0"), not the name part. A store path like
 * "/nix/store/dc04vv14dak1c1r48qa0m23vr9jy8sm0-foo" will be detected if the
 * hash appears anywhere in the scanned content, regardless of the "-foo" suffix.
 *
 * @param accessor Source accessor to read the tree
 * @param rootPath Root path to scan
 * @param refs Set of store paths to search for
 * @param callback Called for each file that contains at least one reference
 */
void scanForReferencesDeep(
    SourceAccessor & accessor,
    const CanonPath & rootPath,
    const StorePathSet & refs,
    std::function<void(FileRefScanResult)> callback);

/**
 * Scan a store path tree and return which references appear in which files.
 *
 * This is a convenience wrapper around the callback-based scanForReferencesDeep()
 * that collects all results into a map for efficient lookups.
 *
 * Note: This function only searches for the hash part of store paths, not the name part.
 * See the callback-based overload for details.
 *
 * @param accessor Source accessor to read the tree
 * @param rootPath Root path to scan
 * @param refs Set of store paths to search for
 * @return Map from file paths to the set of references found in each file
 */
std::map<CanonPath, StorePathSet>
scanForReferencesDeep(SourceAccessor & accessor, const CanonPath & rootPath, const StorePathSet & refs);

} // namespace nix
