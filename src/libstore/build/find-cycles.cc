#include "find-cycles.hh"

#include "nix/store/store-api.hh"
#include "nix/util/file-system.hh"
#include "nix/util/archive.hh"

#include <filesystem>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace nix {

// Hash length in characters (32 for base32-encoded sha256)
static constexpr size_t refLength = StorePath::HashLen;

CycleEdgeScanSink::CycleEdgeScanSink(
    StringSet && hashes, std::map<std::string, StorePath> && backMap, std::string storeDir)
    : RefScanSink(std::move(hashes))
    , hashPathMap(std::move(backMap))
    , storeDir(std::move(storeDir))
{
}

void CycleEdgeScanSink::setCurrentPath(const std::string & path)
{
    currentFilePath = path;
}

void CycleEdgeScanSink::operator()(std::string_view data)
{
    // Track what hashes we've already seen
    auto seenBefore = getResult();

    // Call parent's operator() to do the actual hash searching
    // This reuses all the proven buffer boundary handling logic
    RefScanSink::operator()(data);

    // Check for newly found hashes
    auto seenAfter = getResult();
    for (const auto & hash : seenAfter) {
        if (seenBefore.find(hash) == seenBefore.end()) {
            // This hash was just found in the current file
            // Create an edge from current file to the target
            auto targetPath = storeDir + hash;

            StoreCycleEdge edge;
            edge.push_back(currentFilePath);
            edge.push_back(targetPath);
            edges.push_back(edge);

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
    std::map<std::string, StorePath> hashPathMap;

    // Extract the store directory from the path
    // Example: /run/user/1000/nix-test/store/abc-foo -> /run/user/1000/nix-test/store/
    auto storePrefixPath = std::filesystem::path(path);
    storePrefixPath.remove_filename();
    std::string storePrefix = storePrefixPath.string();

    debug("scanForCycleEdges: storePrefixPath = %s", storePrefixPath.string());
    debug("scanForCycleEdges: storePrefix = %s", storePrefix);

    // Build map of hash -> StorePath and collect hashes to search for
    for (auto & i : refs) {
        std::string hashPart(i.hashPart());
        auto inserted = hashPathMap.emplace(hashPart, i).second;
        assert(inserted);
        hashes.insert(hashPart);
    }

    // Create sink that reuses RefScanSink's hash-finding logic
    CycleEdgeScanSink sink(std::move(hashes), std::move(hashPathMap), storePrefix);

    // Walk the filesystem and scan files using the sink
    scanForCycleEdges2(path, sink);

    // Extract the found edges
    edges = sink.getEdges();
}

/**
 * Recursively walk filesystem and stream files into the sink.
 * This reuses RefScanSink's hash-finding logic instead of reimplementing it.
 */
void scanForCycleEdges2(const std::string & path, CycleEdgeScanSink & sink)
{
    auto st = lstat(path);

    debug("scanForCycleEdges2: scanning path = %s", path);

    if (S_ISREG(st.st_mode)) {
        // Handle regular files - stream contents into sink
        // The sink (RefScanSink) handles all hash detection and buffer management
        sink.setCurrentPath(path);

        AutoCloseFD fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (!fd)
            throw SysError("opening file '%1%'", path);

        // Stream file contents into sink
        // RefScanSink handles buffer boundaries automatically
        std::vector<char> buf(65536);
        while (true) {
            ssize_t n = read(fd.get(), buf.data(), buf.size());
            if (n == -1)
                throw SysError("reading file '%1%'", path);
            if (n == 0)
                break;

            sink(std::string_view(buf.data(), n));
        }
    } else if (S_ISDIR(st.st_mode)) {
        // Handle directories - recursively scan contents
        std::map<std::string, std::string> unhacked;

        for (DirectoryIterator i(path); i != DirectoryIterator(); ++i) {
            std::string entryName = i->path().filename().string();

#ifdef __APPLE__
            // Handle case-insensitive filesystems on macOS
            std::string name(entryName);
            size_t pos = entryName.find(caseHackSuffix);
            if (pos != std::string::npos) {
                debug("removing case hack suffix from '%s'", path + "/" + entryName);
                name.erase(pos);
            }
            if (unhacked.find(name) != unhacked.end()) {
                throw Error(
                    "file name collision between '%1%' and '%2%'", path + "/" + unhacked[name], path + "/" + entryName);
            }
            unhacked[name] = entryName;
#else
            unhacked[entryName] = entryName;
#endif
        }

        for (auto & [name, actualName] : unhacked) {
            debug("scanForCycleEdges2: recursing into %s/%s", path, actualName);
            scanForCycleEdges2(path + "/" + actualName, sink);
        }
    } else if (S_ISLNK(st.st_mode)) {
        // Handle symlinks - stream link target into sink
        std::string linkTarget = readLink(path);

        debug("scanForCycleEdges2: scanning symlink %s -> %s", path, linkTarget);

        sink.setCurrentPath(path);
        sink(std::string_view(linkTarget));
    } else {
        throw Error("file '%1%' has an unsupported type", path);
    }
}

void transformEdgesToMultiedges(StoreCycleEdgeVec & edges, StoreCycleEdgeVec & multiedges)
{
    debug("transformEdgesToMultiedges: processing %lu edges", edges.size());

    for (auto & edge2 : edges) {
        bool edge2Joined = false;

        for (auto & edge1 : multiedges) {
            debug("comparing edge1 (size=%lu) with edge2 (size=%lu)", edge1.size(), edge2.size());

            // Check if edge1.back() == edge2.front()
            // This means we can extend edge1 by appending edge2
            if (edge1.back() == edge2.front()) {
                debug("appending: edge1.back()='%s' == edge2.front()='%s'", edge1.back(), edge2.front());

                // Append all but the first element of edge2 (to avoid duplication)
                for (size_t i = 1; i < edge2.size(); i++) {
                    debug("  appending edge2[%lu] = %s", i, edge2[i]);
                    edge1.push_back(edge2[i]);
                }
                edge2Joined = true;
                break;
            }

            // Check if edge2.back() == edge1.front()
            // This means we can extend edge1 by prepending edge2
            if (edge2.back() == edge1.front()) {
                debug("prepending: edge2.back()='%s' == edge1.front()='%s'", edge2.back(), edge1.front());

                // Prepend all but the last element of edge2 (to avoid duplication)
                for (int i = edge2.size() - 2; i >= 0; i--) {
                    debug("  prepending edge2[%d] = %s", i, edge2[i]);
                    edge1.push_front(edge2[i]);
                }
                edge2Joined = true;
                break;
            }
        }

        if (!edge2Joined) {
            debug("edge2 is new, adding as separate multiedge");
            multiedges.push_back(edge2);
        }
    }

    debug("transformEdgesToMultiedges: result has %lu multiedges", multiedges.size());
}

} // namespace nix
