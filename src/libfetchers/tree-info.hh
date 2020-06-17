#pragma once

#include "path.hh"
#include "hash.hh"

#include <nlohmann/json_fwd.hpp>

namespace nix { class Store; }

namespace nix::fetchers {

struct TreeInfo
{
    Hash narHash;
    std::optional<uint64_t> revCount;
    std::optional<time_t> lastModified;

    bool operator ==(const TreeInfo & other) const
    {
        return
            narHash == other.narHash
            && revCount == other.revCount
            && lastModified == other.lastModified;
    }

    StorePath computeStorePath(Store & store) const;
};

}
