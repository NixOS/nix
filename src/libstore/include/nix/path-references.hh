#pragma once
///@file

#include "nix/references.hh"
#include "nix/path.hh"

namespace nix {

std::pair<StorePathSet, HashResult> scanForReferences(const Path & path, const StorePathSet & refs);

StorePathSet scanForReferences(Sink & toTee, const Path & path, const StorePathSet & refs);

class PathRefScanSink : public RefScanSink
{
    std::map<std::string, StorePath> backMap;

    PathRefScanSink(StringSet && hashes, std::map<std::string, StorePath> && backMap);

public:

    static PathRefScanSink fromPaths(const StorePathSet & refs);

    StorePathSet getResultPaths();
};

}
