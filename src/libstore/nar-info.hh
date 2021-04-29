#pragma once

#include "types.hh"
#include "hash.hh"
#include "path-info.hh"

namespace nix {

class Store;

struct NarInfo : ValidPathInfo
{
    std::string url;
    std::string compression;
    std::optional<Hash> fileHash;
    uint64_t fileSize = 0;
    std::string system;

    NarInfo() = delete;
    NarInfo(const Store & store, StorePathDescriptor && ca, Hash narHash)
        : ValidPathInfo(store, std::move(ca), narHash)
    { }
    NarInfo(StorePath && path, Hash narHash) : ValidPathInfo(std::move(path), narHash) { }
    NarInfo(const ValidPathInfo & info) : ValidPathInfo(info) { }
    NarInfo(const Store & store, const std::string & s, const std::string & whence);

    std::string to_string(const Store & store) const;
};

}
