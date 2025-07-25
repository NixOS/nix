#pragma once

#include "nix/store/store-api.hh"

namespace nix {

struct AsyncPathWriter
{
    virtual StorePath addPath(
        std::string contents, std::string name, StorePathSet references, RepairFlag repair, bool readOnly = false) = 0;

    virtual void waitForPath(const StorePath & path) = 0;

    virtual void waitForAllPaths() = 0;

    static ref<AsyncPathWriter> make(ref<Store> store);
};

} // namespace nix
