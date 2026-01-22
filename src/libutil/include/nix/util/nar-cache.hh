#pragma once

#include "nix/util/hash.hh"
#include "nix/util/nar-accessor.hh"
#include "nix/util/ref.hh"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>

namespace nix {

/**
 * Abstract cache for NAR accessors.
 */
class NarCache
{
protected:
    /**
     * Map from NAR hash to NAR accessor.
     */
    std::map<Hash, ref<NarAccessor>> nars;

public:

    virtual ~NarCache() = default;

    /**
     * Lookup or create a NAR accessor.
     *
     * @param narHash The NAR hash to use as cache key
     * @param populate Function called with a Sink to populate the NAR if not cached
     * @return The cached or newly created accessor
     */
    virtual ref<NarAccessor> getOrInsert(const Hash & narHash, std::function<void(Sink &)> populate) = 0;
};

/**
 * Create an in-memory only NAR cache.
 */
std::unique_ptr<NarCache> makeMemoryNarCache();

} // namespace nix
