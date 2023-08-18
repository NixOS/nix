#include "path-references.hh"
#include "hash.hh"
#include "util.hh"
#include "archive.hh"

#include <map>
#include <cstdlib>
#include <mutex>
#include <algorithm>


namespace nix {


PathRefScanSink::PathRefScanSink(StringSet && hashes, std::map<std::string, StorePath> && backMap)
    : RefScanSink(std::move(hashes))
    , backMap(std::move(backMap))
{ }

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


std::pair<StorePathSet, HashResult> scanForReferences(
    const std::string & path,
    const StorePathSet & refs)
{
    HashSink hashSink { htSHA256 };
    auto found = scanForReferences(hashSink, path, refs);
    auto hash = hashSink.finish();
    return std::pair<StorePathSet, HashResult>(found, hash);
}

StorePathSet scanForReferences(
    Sink & toTee,
    const Path & path,
    const StorePathSet & refs)
{
    PathRefScanSink refsSink = PathRefScanSink::fromPaths(refs);
    TeeSink sink { refsSink, toTee };

    /* Look for the hashes in the NAR dump of the path. */
    dumpPath(path, sink);

    return refsSink.getResultPaths();
}

}
