#pragma once
///@file

#include "nix/store/store-api.hh"
#include "nix/store/references.hh"
#include "nix/util/types.hh"

#include <string>
#include <deque>
#include <vector>

namespace nix {

/**
 * Represents a cycle edge as a sequence of file paths.
 * Uses deque to allow efficient prepend/append when joining edges.
 *
 * Example: {"/nix/store/abc-foo/file1", "/nix/store/def-bar/file2"}
 * represents a reference from file1 to file2.
 */
typedef std::deque<std::string> StoreCycleEdge;

/**
 * A collection of cycle edges found during scanning.
 */
typedef std::vector<StoreCycleEdge> StoreCycleEdgeVec;

/**
 * A sink that extends RefScanSink to track file paths where references are found.
 *
 * This reuses the existing reference scanning logic from RefScanSink, but adds
 * tracking of which file contains which reference. This is essential for providing
 * detailed cycle error messages.
 */
class CycleEdgeScanSink : public RefScanSink
{
    std::string currentFilePath;
    std::string storeDir;

    // Track hashes we've already recorded for current file
    // to avoid duplicates
    StringSet recordedForCurrentFile;

public:
    StoreCycleEdgeVec edges;

    CycleEdgeScanSink(StringSet && hashes, std::string storeDir);

    /**
     * Set the current file path being scanned.
     * Must be called before processing each file.
     */
    void setCurrentPath(const std::string & path);

    /**
     * Override to intercept when hashes are found and record the file location.
     */
    void operator()(std::string_view data) override;

    /**
     * Get the accumulated cycle edges.
     */
    StoreCycleEdgeVec && getEdges();
};

/**
 * Scan output paths to find cycle edges with detailed file paths.
 *
 * This is the second pass of cycle detection. The first pass (scanForReferences)
 * detects that a cycle exists. This function provides detailed information about
 * where the cycles occur in the actual file system.
 *
 * @param path The store path to scan (e.g., an output directory)
 * @param refs The set of potentially referenced store paths
 * @param edges Output parameter that accumulates found cycle edges
 */
void scanForCycleEdges(const Path & path, const StorePathSet & refs, StoreCycleEdgeVec & edges);

/**
 * Recursively scan files and directories for hash references.
 *
 * This function walks the file system tree, streaming file contents into
 * the provided sink which performs the actual hash detection. This reuses
 * the existing RefScanSink infrastructure for robustness.
 *
 * @param path Current path being scanned
 * @param sink The CycleEdgeScanSink that will detect and record hash references
 */
void scanForCycleEdges2(const std::string & path, CycleEdgeScanSink & sink);

/**
 * Transform individual edges into connected multi-edges (paths).
 *
 * Takes a list of edges like [A→B, B→C, C→A] and connects them into
 * longer paths like [A→B→C→A]. This makes it easier to visualize the
 * actual cycle paths.
 *
 * The algorithm is greedy: it tries to extend each edge by finding
 * matching edges to prepend or append.
 *
 * @param edges Input edges to transform
 * @param multiedges Output parameter with connected paths
 */
void transformEdgesToMultiedges(StoreCycleEdgeVec & edges, StoreCycleEdgeVec & multiedges);

} // namespace nix
