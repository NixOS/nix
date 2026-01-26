#pragma once

#include "nix/util/hash.hh"
#include "nix/util/nar-accessor.hh"

#include <filesystem>

namespace nix {

class NarCache
{
    const std::filesystem::path cacheDir;

    std::filesystem::path makeCacheFile(const Hash & narHash, const std::string & ext);

public:

    NarCache(std::filesystem::path cacheDir);

    void upsertNar(const Hash & narHash, Source & source);

    void upsertNarListing(const Hash & narHash, std::string_view narListingData);

    // FIXME: use a sink.
    std::optional<std::string> getNar(const Hash & narHash);

    // FIXME: use a sink.
    GetNarBytes getNarBytes(const Hash & narHash);

    std::optional<std::string> getNarListing(const Hash & narHash);
};

} // namespace nix
