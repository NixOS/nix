#pragma once
///@file

#include "nix/util/source-accessor.hh"
#include "nix/util/ref.hh"
#include "nix/util/nar-cache.hh"
#include "nix/store/store-api.hh"

namespace nix {

class RemoteFSAccessor : public SourceAccessor
{
    ref<Store> store;

    /**
     * Map from store path hash part to NAR hash. Used to then look up
     * in the NAR cache. The indirection allows avoiding opening multiple
     * redundant NAR accessors for the same NAR.
     */
    std::map<std::string, Hash, std::less<>> narHashes;

    NarCache narCache;

    bool requireValidPath;

    std::pair<ref<SourceAccessor>, CanonPath> fetch(const CanonPath & path);

    friend struct BinaryCacheStore;

public:

    /**
     * @return nullptr if the store does not contain any object at that path.
     */
    std::shared_ptr<SourceAccessor> accessObject(const StorePath & path);

    RemoteFSAccessor(
        ref<Store> store, bool requireValidPath = true, std::optional<std::filesystem::path> cacheDir = {});

    std::optional<Stat> maybeLstat(const CanonPath & path) override;

    DirEntries readDirectory(const CanonPath & path) override;

    void readFile(const CanonPath & path, Sink & sink, fun<void(uint64_t)> sizeCallback) override;

    using SourceAccessor::readFile;

    std::string readLink(const CanonPath & path) override;
};

} // namespace nix
