#pragma once

#include "nix/util/fun.hh"
#include "nix/util/hash.hh"
#include "nix/util/nar-accessor.hh"
#include "nix/util/ref.hh"
#include "nix/util/source-accessor.hh"

#include <filesystem>
#include <functional>
#include <map>
#include <optional>

namespace nix {

/**
 * A cache for NAR accessors with optional disk caching.
 */
class NarCache
{
    /**
     * Optional directory for caching NARs and listings on disk.
     */
    std::optional<std::filesystem::path> cacheDir;

    /**
     * Map from NAR hash to NAR accessor.
     */
    std::map<Hash, ref<SourceAccessor>> nars;

public:

    /**
     * Create a NAR cache with an optional cache directory for disk storage.
     */
    NarCache(std::optional<std::filesystem::path> cacheDir = {});

    /**
     * Lookup or create a NAR accessor, optionally using disk cache.
     *
     * @param narHash The NAR hash to use as cache key
     * @param populate Function called with a Sink to populate the NAR if not cached
     * @return The cached or newly created accessor
     */
    ref<SourceAccessor> getOrInsert(const Hash & narHash, fun<void(Sink &)> populate);
};

} // namespace nix
