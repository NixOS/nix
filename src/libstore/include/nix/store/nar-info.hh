#pragma once

#include "nix/util/types.hh"
#include "nix/util/hash.hh"
#include "nix/store/path-info.hh"

namespace nix {

class Store;

struct NarInfo : ValidPathInfo
{
    std::string url;
    std::string compression;
    std::optional<Hash> fileHash;
    uint64_t fileSize = 0;

    NarInfo() = delete;
    NarInfo(StorePath && path, Hash narHash) : ValidPathInfo(std::move(path), narHash) { }
    NarInfo(const ValidPathInfo & info) : ValidPathInfo(info) { }
    NarInfo(const Store & store, const std::string & s, const std::string & whence);

    std::string to_string(const Store & store) const;
};

}
