#include "nix/store/path-references.hh"
#include "nix/store/dependency-graph.hh"
#include "nix/util/hash.hh"
#include "nix/util/archive.hh"
#include "nix/util/source-accessor.hh"
#include "nix/util/canon-path.hh"
#include "nix/util/logging.hh"

#include <map>
#include <cstdlib>
#include <mutex>
#include <algorithm>
#include <functional>

namespace nix {

PathRefScanSink::PathRefScanSink(StringSet && hashes, std::map<std::string, StorePath> && backMap)
    : RefScanSink(std::move(hashes))
    , backMap(std::move(backMap))
{
}

PathRefScanSink PathRefScanSink::fromPaths(const StorePathSet & refs)
{
    StringSet hashes;
    std::map<std::string, StorePath> backMap;

    for (auto & i : refs) {
        std::string hashPart(i.hashPart());
        auto inserted = backMap.emplace(hashPart, i).second;
        assert(inserted);
        hashes.insert(hashPart);
    }

    return PathRefScanSink(std::move(hashes), std::move(backMap));
}

StorePathSet PathRefScanSink::getResultPaths()
{
    /* Map the hashes found back to their store paths. */
    StorePathSet found;
    for (auto & i : getResult()) {
        auto j = backMap.find(i);
        assert(j != backMap.end());
        found.insert(j->second);
    }

    return found;
}

StorePathSet scanForReferences(Sink & toTee, const Path & path, const StorePathSet & refs)
{
    PathRefScanSink refsSink = PathRefScanSink::fromPaths(refs);
    TeeSink sink{refsSink, toTee};

    /* Look for the hashes in the NAR dump of the path. */
    dumpPath(path, sink);

    return refsSink.getResultPaths();
}

void scanForReferencesDeep(
    SourceAccessor & accessor,
    const CanonPath & rootPath,
    const StorePathSet & refs,
    std::function<void(const FileRefScanResult &)> callback)
{
    // Recursive tree walker
    auto walk = [&](this auto & self, const CanonPath & path) -> void {
        auto stat = accessor.lstat(path);

        switch (stat.type) {
        case SourceAccessor::tRegular: {
            // Create a fresh sink for each file to independently detect references.
            // RefScanSink accumulates found hashes globally - once a hash is found,
            // it remains in the result set. If we reused the same sink across files,
            // we couldn't distinguish which files contain which references, as a hash
            // found in an earlier file wouldn't be reported when found in later files.
            PathRefScanSink sink = PathRefScanSink::fromPaths(refs);

            // Scan this file by streaming its contents through the sink
            accessor.readFile(path, sink);

            // Get the references found in this file
            auto foundRefs = sink.getResultPaths();

            // Report if we found anything in this file
            if (!foundRefs.empty()) {
                debug("scanForReferencesDeep: found %d references in %s", foundRefs.size(), path.abs());
                callback(FileRefScanResult{.filePath = path, .foundRefs = std::move(foundRefs)});
            }
            break;
        }

        case SourceAccessor::tDirectory: {
            // Recursively scan directory contents
            auto entries = accessor.readDirectory(path);
            for (const auto & [name, entryType] : entries) {
                self(path / name);
            }
            break;
        }

        case SourceAccessor::tSymlink: {
            // Create a fresh sink for the symlink target (same reason as regular files)
            PathRefScanSink sink = PathRefScanSink::fromPaths(refs);

            // Scan symlink target for references
            auto target = accessor.readLink(path);
            sink(std::string_view(target));

            // Get the references found in this symlink target
            auto foundRefs = sink.getResultPaths();

            if (!foundRefs.empty()) {
                debug("scanForReferencesDeep: found %d references in symlink %s", foundRefs.size(), path.abs());
                callback(FileRefScanResult{.filePath = path, .foundRefs = std::move(foundRefs)});
            }
            break;
        }

        case SourceAccessor::tChar:
        case SourceAccessor::tBlock:
        case SourceAccessor::tSocket:
        case SourceAccessor::tFifo:
        case SourceAccessor::tUnknown:
        default:
            throw Error("file '%s' has an unsupported type", path.abs());
        }
    };

    // Start the recursive walk from the root
    walk(rootPath);
}

std::map<CanonPath, StorePathSet>
scanForReferencesDeep(SourceAccessor & accessor, const CanonPath & rootPath, const StorePathSet & refs)
{
    std::map<CanonPath, StorePathSet> results;

    scanForReferencesDeep(accessor, rootPath, refs, [&](const FileRefScanResult & result) {
        results[result.filePath] = result.foundRefs;
    });

    return results;
}

DependencyGraph<StorePath, FileListEdgeProperty> buildStorePathGraphFromScan(
    SourceAccessor & accessor, const CanonPath & rootPath, const StorePath & rootStorePath, const StorePathSet & refs)
{
    DependencyGraph<StorePath, FileListEdgeProperty> graph;

    scanForReferencesDeep(accessor, rootPath, refs, [&](const FileRefScanResult & result) {
        // All files in this scan belong to rootStorePath
        for (const auto & foundRef : result.foundRefs) {
            // Add StorePath -> StorePath edge with file metadata
            FileListEdgeProperty edgeProp;
            edgeProp.files.push_back(result.filePath);
            graph.addEdge(rootStorePath, foundRef, std::move(edgeProp));

            debug(
                "buildStorePathGraphFromScan: %s (in %s) → %s",
                rootStorePath.to_string(),
                result.filePath.abs(),
                foundRef.to_string());
        }
    });

    return graph;
}

} // namespace nix
