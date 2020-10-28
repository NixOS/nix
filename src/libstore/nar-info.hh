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
    NarInfo(StorePath && path, ContentAddresses cas) : ValidPathInfo(std::move(path), cas) { }
    NarInfo(const ValidPathInfo & info) : ValidPathInfo(info) { }
    NarInfo(const Store & store, const std::string & s, const std::string & whence);

    std::string to_string(const Store & store) const;
};

}
