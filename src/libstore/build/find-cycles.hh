#pragma once
///@file

#include "nix/store/store-api.hh"
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
 * This function walks the file system tree and searches for store path hashes
 * in file contents, symlinks, etc. When a hash is found, it attempts to
 * reconstruct the full store path by checking what files actually exist.
 *
 * @param path Current path being scanned
 * @param hashes Set of hash strings to look for (32-char base32 hashes)
 * @param edges Output parameter that accumulates found cycle edges
 * @param storeDir The store directory prefix (e.g., "/nix/store/")
 */
void scanForCycleEdges2(std::string path, const StringSet & hashes, StoreCycleEdgeVec & edges, std::string storeDir);

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
