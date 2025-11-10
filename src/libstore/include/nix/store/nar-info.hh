#pragma once
///@file

#include "nix/util/types.hh"
#include "nix/util/hash.hh"
#include "nix/store/path-info.hh"

namespace nix {

struct StoreDirConfig;

struct NarInfo : ValidPathInfo
{
    std::string url;
    std::string compression;
    std::optional<Hash> fileHash;
    uint64_t fileSize = 0;

    NarInfo() = delete;

    NarInfo(ValidPathInfo info)
        : ValidPathInfo{std::move(info)}
    {
    }

    NarInfo(StorePath path, Hash narHash)
        : NarInfo{ValidPathInfo{std::move(path), UnkeyedValidPathInfo(narHash)}}
    {
    }

    static NarInfo
    makeFromCA(const StoreDirConfig & store, std::string_view name, ContentAddressWithReferences ca, Hash narHash)
    {
        return ValidPathInfo::makeFromCA(store, std::move(name), std::move(ca), narHash);
    }

    NarInfo(const StoreDirConfig & store, const std::string & s, const std::string & whence);

    bool operator==(const NarInfo &) const = default;
    // TODO libc++ 16 (used by darwin) missing `std::optional::operator <=>`, can't do yet
    // auto operator <=>(const NarInfo &) const = default;

    std::string to_string(const StoreDirConfig & store) const;

    nlohmann::json toJSON(const StoreDirConfig & store, bool includeImpureInfo) const override;
    static NarInfo fromJSON(const StoreDirConfig & store, const StorePath & path, const nlohmann::json & json);
};

} // namespace nix
