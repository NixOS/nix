#pragma once

//#include "hash.hh"
#include "path.hh"

#include <string>
#include <deque>

namespace nix {

// see nix/src/libstore/references.hh
// first pass: fast on success
//std::pair<StorePathSet, HashResult> scanForReferences(const Path & path, const StorePathSet & refs);
//StorePathSet scanForReferences(Sink & toTee, const Path & path, const StorePathSet & refs);

//typedef std::pair<std::string, std::string> StoreCycleEdge;
// need deque to join edges
typedef std::deque<std::string> StoreCycleEdge;
typedef std::vector<StoreCycleEdge> StoreCycleEdgeVec;

// second pass: get exact file paths of cycles
void scanForCycleEdges(
    const Path & path,
    const StorePathSet & refs,
    StoreCycleEdgeVec & edges
);

void scanForCycleEdges2(
    std::string path,
    const StringSet & hashes,
    StoreCycleEdgeVec & seen,
    std::string storePrefix
);

void transformEdgesToMultiedges(
    StoreCycleEdgeVec & edges,
    StoreCycleEdgeVec & multiedges
);

}
