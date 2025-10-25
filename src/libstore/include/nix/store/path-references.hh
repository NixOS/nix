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
 * @param accessor Source accessor to read the tree
 * @param rootPath Root path to scan
 * @param refs Set of store paths to search for
 * @param callback Called for each file that contains at least one reference
 */
void scanForReferencesDeep(
    SourceAccessor & accessor,
    const CanonPath & rootPath,
    const StorePathSet & refs,
    std::function<void(const FileRefScanResult &)> callback);

/**
 * Scan a store path tree and return which references appear in which files.
 *
 * This is a convenience wrapper around the callback-based scanForReferencesDeep()
 * that collects all results into a vector.
 *
 * @param accessor Source accessor to read the tree
 * @param rootPath Root path to scan
 * @param refs Set of store paths to search for
 * @return Vector of results, one per file that contains at least one reference
 */
std::vector<FileRefScanResult>
scanForReferencesDeep(SourceAccessor & accessor, const CanonPath & rootPath, const StorePathSet & refs);

} // namespace nix
