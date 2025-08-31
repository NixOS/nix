#include "nix/store/path-references.hh"
#include "nix/util/hash.hh"
#include "nix/util/archive.hh"

#include <map>
#include <cstdlib>
#include <mutex>
#include <algorithm>

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

} // namespace nix
