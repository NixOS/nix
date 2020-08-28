#pragma once

#include "fs-accessor.hh"
#include "ref.hh"
#include "store-api.hh"

namespace nix {

class RemoteFSAccessor : public FSAccessor
{
    ref<Store> store;

    std::map<std::string, ref<FSAccessor>> nars;

    Path cacheDir;

    std::pair<ref<FSAccessor>, Path> fetch(const Path & path_);

    friend class BinaryCacheStore;

    Path makeCacheFile(std::string_view hashPart, const std::string & ext);

    void addToCache(std::string_view hashPart, const std::string & nar,
        ref<FSAccessor> narAccessor);

public:

    RemoteFSAccessor(ref<Store> store,
        const /* FIXME: use std::optional */ Path & cacheDir = "");

    Stat stat(const Path & path) override;

    StringSet readDirectory(const Path & path) override;

    std::string readFile(const Path & path) override;

    std::string readLink(const Path & path) override;
};

}
