#pragma once

#include "types.hh"
#include "hash.hh"
#include "store-api.hh"

namespace nix {

struct NarInfo : ValidPathInfo
{
    std::string url;
    std::string compression;
    std::optional<Hash> fileHash;
    uint64_t fileSize = 0;
    std::string system;

    NarInfo() = delete;
    NarInfo(const Store & store, StorePathDescriptor && ca)
        : ValidPathInfo(store, std::move(ca)) { }
    NarInfo(StorePath && path) : ValidPathInfo(std::move(path)) { }
    NarInfo(const ValidPathInfo & info) : ValidPathInfo(info) { }
    NarInfo(const Store & store, const std::string & s, const std::string & whence);

    std::string to_string(const Store & store) const;
};

}
