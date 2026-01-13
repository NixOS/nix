#pragma once
///@file

#include "nix/util/source-accessor.hh"
#include "nix/util/ref.hh"
#include "nix/store/store-api.hh"

namespace nix {

class RemoteFSAccessor : public SourceAccessor
{
    ref<Store> store;

    /**
     * Map from store path hash part to NAR hash. Used to then look up
     * in `nars`. The indirection allows avoiding opening multiple
     * redundant NAR accessors for the same NAR.
     */
    std::map<std::string, Hash, std::less<>> narHashes;

    /**
     * Map from NAR hash to NAR accessor.
     */
    std::map<Hash, ref<SourceAccessor>> nars;

    bool requireValidPath;

    std::optional<std::filesystem::path> cacheDir;

    std::pair<ref<SourceAccessor>, CanonPath> fetch(const CanonPath & path);

    friend struct BinaryCacheStore;

    std::filesystem::path makeCacheFile(const Hash & narHash, const std::string & ext);

public:

    /**
     * @return nullptr if the store does not contain any object at that path.
     */
    std::shared_ptr<SourceAccessor> accessObject(const StorePath & path);

    RemoteFSAccessor(
        ref<Store> store, bool requireValidPath = true, std::optional<std::filesystem::path> cacheDir = {});

    std::optional<Stat> maybeLstat(const CanonPath & path) override;

    DirEntries readDirectory(const CanonPath & path) override;

    std::string readFile(const CanonPath & path) override;

    std::string readLink(const CanonPath & path) override;
};

} // namespace nix
