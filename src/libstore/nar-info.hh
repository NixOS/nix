#pragma once

#include "types.hh"
#include "hash.hh"
#include "store-api.hh"

namespace nix {

struct NarInfo : ValidPathInfo
{
    std::string url;
    std::string compression;
    Hash fileHash;
    uint64_t fileSize = 0;
    std::string system;

    NarInfo() = delete;
    NarInfo(StorePath && path) : ValidPathInfo(std::move(path)) { }
    NarInfo(const ValidPathInfo & info) : ValidPathInfo(info) { }
    NarInfo(const Store & store, std::string_view s, std::string_view whence);

    std::string to_string(const Store & store) const;
};

}
