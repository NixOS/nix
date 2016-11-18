#pragma once

#include "fs-accessor.hh"
#include "ref.hh"
#include "store-api.hh"

namespace nix {

class RemoteFSAccessor : public FSAccessor
{
    ref<Store> store;

    std::map<Path, ref<FSAccessor>> nars;

    std::pair<ref<FSAccessor>, Path> fetch(const Path & path_);
public:

    RemoteFSAccessor(ref<Store> store);

    Stat stat(const Path & path) override;

    StringSet readDirectory(const Path & path) override;

    std::string readFile(const Path & path) override;

    std::string readLink(const Path & path) override;
};

}
