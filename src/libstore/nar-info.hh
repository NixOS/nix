#pragma once
///@file

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

    NarInfo() = delete;
    NarInfo(const Store & store, std::string name, ContentAddressWithReferences ca, Hash narHash)
        : ValidPathInfo(store, std::move(name), std::move(ca), narHash)
    { }
    NarInfo(StorePath path, Hash narHash) : ValidPathInfo(std::move(path), narHash) { }
    NarInfo(const ValidPathInfo & info) : ValidPathInfo(info) { }
    NarInfo(const Store & store, const std::string & s, const std::string & whence);

    DECLARE_CMP(NarInfo);

    std::string to_string(const Store & store) const;

    nlohmann::json toJSON(
        const Store & store,
        bool includeImpureInfo,
        HashFormat hashFormat) const override;
    static NarInfo fromJSON(
        const Store & store,
        const StorePath & path,
        const nlohmann::json & json);
};

}
