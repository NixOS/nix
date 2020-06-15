#pragma once

#include "fs-accessor.hh"
#include "ref.hh"
#include "store-api.hh"

namespace nix {

class RemoteFSAccessor : public FSAccessor
{
    ref<Store> store;

    std::map<Path, ref<FSAccessor>> nars;

    Path cacheDir;

    std::pair<ref<FSAccessor>, Path> fetch(PathView path_);

    friend class BinaryCacheStore;

    Path makeCacheFile(PathView storePath, std::string_view ext);

    void addToCache(PathView storePath, std::string_view nar,
        ref<FSAccessor> narAccessor);

public:

    RemoteFSAccessor(ref<Store> store,
        const /* FIXME: use std::optional */ Path & cacheDir = "");

    Stat stat(PathView path) override;

    StringSet readDirectory(PathView path) override;

    std::string readFile(PathView path) override;

    std::string readLink(PathView path) override;
};

}
