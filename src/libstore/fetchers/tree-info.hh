#pragma once

namespace nix {

struct TreeInfo
{
    Hash narHash;
    std::optional<Hash> rev; // FIXME: remove
    std::optional<uint64_t> revCount;
    std::optional<time_t> lastModified;

    bool operator ==(const TreeInfo & other) const
    {
        return
            narHash == other.narHash
            && rev == other.rev
            && revCount == other.revCount
            && lastModified == other.lastModified;
    }
};

}
