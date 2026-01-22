#include "nix/util/nar-cache.hh"

namespace nix {

namespace {

/**
 * In-memory only NAR cache (private implementation).
 */
class MemoryNarCache : public NarCache
{
public:
    ref<NarAccessor> getOrInsert(const Hash & narHash, std::function<void(Sink &)> populate) override;
};

} // anonymous namespace

ref<NarAccessor> MemoryNarCache::getOrInsert(const Hash & narHash, std::function<void(Sink &)> populate)
{
    // Check in-memory cache first
    if (auto * accessor = get(nars, narHash))
        return *accessor;

    StringSink sink;
    populate(sink);
    auto accessor = makeNarAccessor(std::move(sink.s));
    nars.emplace(narHash, accessor);
    return accessor;
}

std::unique_ptr<NarCache> makeMemoryNarCache()
{
    return std::make_unique<MemoryNarCache>();
}

} // namespace nix
