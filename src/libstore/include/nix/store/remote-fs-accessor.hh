#pragma once
///@file

#include "nix/util/source-accessor.hh"
#include "nix/util/ref.hh"
#include "nix/store/store-api.hh"

namespace nix {

class RemoteFSAccessor : public SourceAccessor
{
    ref<Store> store;

    std::map<std::string, ref<SourceAccessor>> nars;

    bool requireValidPath;

    std::optional<std::filesystem::path> cacheDir;

    std::pair<ref<SourceAccessor>, CanonPath> fetch(const CanonPath & path);

    friend struct BinaryCacheStore;

    std::filesystem::path makeCacheFile(std::string_view hashPart, const std::string & ext);

    ref<SourceAccessor> addToCache(std::string_view hashPart, std::string && nar);

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
