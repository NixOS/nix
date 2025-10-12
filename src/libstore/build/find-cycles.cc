#include "nix/store/build/find-cycles.hh"

#include "nix/store/store-api.hh"
#include "nix/util/file-system.hh"
#ifdef __APPLE__
#  include "nix/util/archive.hh" // For caseHackSuffix
#endif

#include <algorithm>
#include <filesystem>
#include <map>

namespace nix {

CycleEdgeScanSink::CycleEdgeScanSink(StringSet && hashes, std::string storeDir)
    : RefScanSink(std::move(hashes))
    , storeDir(std::move(storeDir))
{
}

void CycleEdgeScanSink::setCurrentPath(const std::string & path)
{
    currentFilePath = path;
    // Clear tracking for new file
    recordedForCurrentFile.clear();
}

void CycleEdgeScanSink::operator()(std::string_view data)
{
    // Call parent's operator() to do the actual hash searching
    // This reuses all the proven buffer boundary handling logic
    RefScanSink::operator()(data);

    // Check which hashes have been found and not yet recorded for this file
    // getResult() returns the set of ALL hashes found so far
    for (const auto & hash : getResult()) {
        if (recordedForCurrentFile.insert(hash).second) {
            // This hash was just found and not yet recorded for current file
            // Create an edge from current file to the target
            auto targetPath = storeDir + hash;

            edges.push_back({currentFilePath, targetPath});

            debug("found cycle edge: %s → %s (hash: %s)", currentFilePath, targetPath, hash);
        }
    }
}

StoreCycleEdgeVec && CycleEdgeScanSink::getEdges()
{
    return std::move(edges);
}

void scanForCycleEdges(const Path & path, const StorePathSet & refs, StoreCycleEdgeVec & edges)
{
    StringSet hashes;

    // Extract the store directory from the path
    // Example: /run/user/1000/nix-test/store/abc-foo -> /run/user/1000/nix-test/store/
    auto storePrefixPath = std::filesystem::path(path);
    storePrefixPath.remove_filename();
    std::string storePrefix = storePrefixPath.string();

    debug("scanForCycleEdges: storePrefixPath = %s", storePrefixPath.string());
    debug("scanForCycleEdges: storePrefix = %s", storePrefix);

    // Collect hashes to search for
    for (auto & i : refs) {
        hashes.insert(std::string(i.hashPart()));
    }

    // Create sink that reuses RefScanSink's hash-finding logic
    CycleEdgeScanSink sink(std::move(hashes), storePrefix);

    // Walk the filesystem and scan files using the sink
    walkAndScanPath(std::filesystem::path(path), sink);

    // Extract the found edges
    edges = sink.getEdges();
}

/**
 * Recursively walk filesystem and stream files into the sink.
 * This reuses RefScanSink's hash-finding logic instead of reimplementing it.
 */
void walkAndScanPath(const std::filesystem::path & path, CycleEdgeScanSink & sink)
{
    auto status = std::filesystem::symlink_status(path);

    debug("walkAndScanPath: scanning path = %s", path.string());

    if (std::filesystem::is_regular_file(status)) {
        // Handle regular files - stream contents into sink
        // The sink (RefScanSink) handles all hash detection and buffer management
        sink.setCurrentPath(path.string());

        // Use Nix's portable readFile that streams into a sink
        // This handles all file I/O portably across platforms
        readFile(path.string(), sink);
    } else if (std::filesystem::is_directory(status)) {
        // Handle directories - recursively scan contents
        std::map<std::string, std::string> unhacked;

        for (DirectoryIterator i(path); i != DirectoryIterator(); ++i) {
            std::string entryName = i->path().filename().string();

#ifdef __APPLE__
            // Handle case-insensitive filesystems on macOS
            std::string name(entryName);
            size_t pos = entryName.find(caseHackSuffix);
            if (pos != std::string::npos) {
                debug("removing case hack suffix from '%s'", (path / entryName).string());
                name.erase(pos);
            }
            if (unhacked.find(name) != unhacked.end()) {
                throw Error(
                    "file name collision between '%1%' and '%2%'",
                    (path / unhacked[name]).string(),
                    (path / entryName).string());
            }
            unhacked[name] = entryName;
#else
            unhacked[entryName] = entryName;
#endif
        }

        for (auto & [name, actualName] : unhacked) {
            debug("walkAndScanPath: recursing into %s/%s", path.string(), actualName);
            walkAndScanPath(path / actualName, sink);
        }
    } else if (std::filesystem::is_symlink(status)) {
        // Handle symlinks - stream link target into sink
        auto linkTarget = std::filesystem::read_symlink(path).string();

        debug("walkAndScanPath: scanning symlink %s -> %s", path.string(), linkTarget);

        sink.setCurrentPath(path.string());
        sink(std::string_view(linkTarget));
    } else {
        throw Error("file '%1%' has an unsupported type", path);
    }
}

void transformEdgesToMultiedges(StoreCycleEdgeVec & edges, StoreCycleEdgeVec & multiedges)
{
    debug("transformEdgesToMultiedges: processing %lu edges", edges.size());

    // Maps to track path endpoints for efficient joining
    // Key: node name, Value: index into multiedges vector
    std::map<std::string, size_t> pathStartingAt; // Maps start node -> path index
    std::map<std::string, size_t> pathEndingAt;   // Maps end node -> path index

    for (auto & edge : edges) {
        if (edge.empty())
            continue;

        const std::string & edgeStart = edge.front();
        const std::string & edgeEnd = edge.back();

        // Check if this edge can connect to existing paths
        auto startIt = pathEndingAt.find(edgeStart);
        auto endIt = pathStartingAt.find(edgeEnd);

        bool canPrepend = (startIt != pathEndingAt.end());
        bool canAppend = (endIt != pathStartingAt.end());

        if (canPrepend && canAppend && startIt->second == endIt->second) {
            // Edge connects a path to itself - append it to form a cycle
            size_t pathIdx = startIt->second;
            auto & path = multiedges[pathIdx];
            // Append all but first element of edge (first element is duplicate)
            path.insert(path.end(), std::next(edge.begin()), edge.end());
            // Update the end point (start point stays the same for a cycle)
            pathEndingAt.erase(startIt);
            pathEndingAt[edgeEnd] = pathIdx;
        } else if (canPrepend && canAppend) {
            // Edge joins two different paths - merge them
            size_t prependIdx = startIt->second;
            size_t appendIdx = endIt->second;
            auto & prependPath = multiedges[prependIdx];
            auto & appendPath = multiedges[appendIdx];

            // Save endpoint before modifying appendPath
            const std::string appendPathEnd = appendPath.back();
            const std::string appendPathStart = appendPath.front();

            // Append edge (without first element) to prependPath
            prependPath.insert(prependPath.end(), std::next(edge.begin()), edge.end());
            // Append appendPath (without first element) to prependPath
            prependPath.insert(prependPath.end(), std::next(appendPath.begin()), appendPath.end());

            // Update maps: prependPath now ends where appendPath ended
            pathEndingAt.erase(startIt);
            pathEndingAt[appendPathEnd] = prependIdx;
            pathStartingAt.erase(appendPathStart);

            // Mark appendPath for removal by clearing it
            appendPath.clear();
        } else if (canPrepend) {
            // Edge extends an existing path at its end
            size_t pathIdx = startIt->second;
            auto & path = multiedges[pathIdx];
            // Append all but first element of edge (first element is duplicate)
            path.insert(path.end(), std::next(edge.begin()), edge.end());
            // Update the end point
            pathEndingAt.erase(startIt);
            pathEndingAt[edgeEnd] = pathIdx;
        } else if (canAppend) {
            // Edge extends an existing path at its start
            size_t pathIdx = endIt->second;
            auto & path = multiedges[pathIdx];
            // Prepend all but last element of edge (last element is duplicate)
            path.insert(path.begin(), edge.begin(), std::prev(edge.end()));
            // Update the start point
            pathStartingAt.erase(endIt);
            pathStartingAt[edgeStart] = pathIdx;
        } else {
            // Edge doesn't connect to anything - start a new path
            size_t newIdx = multiedges.size();
            multiedges.push_back(edge);
            pathStartingAt[edgeStart] = newIdx;
            pathEndingAt[edgeEnd] = newIdx;
        }
    }

    // Remove empty paths (those that were merged into others)
    multiedges.erase(
        std::remove_if(multiedges.begin(), multiedges.end(), [](const StoreCycleEdge & p) { return p.empty(); }),
        multiedges.end());

    debug("transformEdgesToMultiedges: result has %lu multiedges", multiedges.size());
}

} // namespace nix
