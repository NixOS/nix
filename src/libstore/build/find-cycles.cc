#include "nix/store/build/find-cycles.hh"

#include "nix/store/store-api.hh"
#include "nix/util/file-system.hh"
#ifdef __APPLE__
#  include "nix/util/archive.hh" // For caseHackSuffix
#endif

#include <filesystem>

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

    // First pass: join edges to multiedges
    for (auto & edge2 : edges) {
        bool edge2Joined = false;

        for (auto & edge1 : multiedges) {
            // Check if edge1.back() == edge2.front()
            // This means we can extend edge1 by appending edge2
            if (edge1.back() == edge2.front()) {
                // Append all but the first element of edge2 (to avoid duplication)
                for (size_t i = 1; i < edge2.size(); i++) {
                    edge1.push_back(edge2[i]);
                }
                edge2Joined = true;
                break;
            }

            // Check if edge2.back() == edge1.front()
            // This means we can extend edge1 by prepending edge2
            if (edge2.back() == edge1.front()) {
                // Prepend all but the last element of edge2 (to avoid duplication)
                for (int i = edge2.size() - 2; i >= 0; i--) {
                    edge1.push_front(edge2[i]);
                }
                edge2Joined = true;
                break;
            }
        }

        if (!edge2Joined) {
            multiedges.push_back(edge2);
        }
    }

    // Second pass: merge multiedges that can now be connected
    // After joining edges, some multiedges might now be connectable
    bool merged = true;
    while (merged) {
        merged = false;
        for (size_t i = 0; i < multiedges.size() && !merged; i++) {
            for (size_t j = i + 1; j < multiedges.size() && !merged; j++) {
                auto & path1 = multiedges[i];
                auto & path2 = multiedges[j];

                if (path1.back() == path2.front()) {
                    // Append path2 to path1
                    for (size_t k = 1; k < path2.size(); k++) {
                        path1.push_back(path2[k]);
                    }
                    multiedges.erase(multiedges.begin() + j);
                    merged = true;
                } else if (path2.back() == path1.front()) {
                    // Prepend path2 to path1
                    for (int k = path2.size() - 2; k >= 0; k--) {
                        path1.push_front(path2[k]);
                    }
                    multiedges.erase(multiedges.begin() + j);
                    merged = true;
                }
            }
        }
    }

    debug("transformEdgesToMultiedges: result has %lu multiedges", multiedges.size());
}

} // namespace nix
