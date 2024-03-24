#pragma once
///@file

#include "source-accessor.hh"
#include "ref.hh"
#include "store-api.hh"

namespace nix {

class RemoteFSAccessor : public SourceAccessor
{
    ref<Store> store;

    std::map<std::string, ref<SourceAccessor>> nars;

    bool requireValidPath;

    Path cacheDir;

    std::pair<ref<SourceAccessor>, CanonPath> fetch(const CanonPath & path);

    friend class BinaryCacheStore;

    Path makeCacheFile(std::string_view hashPart, const std::string & ext);

    ref<SourceAccessor> addToCache(std::string_view hashPart, std::string && nar);

public:

    RemoteFSAccessor(ref<Store> store,
        bool requireValidPath = true,
        const /* FIXME: use std::optional */ Path & cacheDir = "");

    std::optional<Stat> maybeLstat(const CanonPath & path) override;

    DirEntries readDirectory(const CanonPath & path) override;

    std::string readFile(const CanonPath & path) override;

    std::string readLink(const CanonPath & path) override;
};

}
