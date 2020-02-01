#pragma once

namespace nix {

struct TreeInfo
{
    Hash narHash;
    std::optional<Hash> rev;
    std::optional<uint64_t> revCount;
    std::optional<time_t> lastModified;
};

}
