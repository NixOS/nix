#pragma once
///@file

#include "nix/util/types.hh"
#include "nix/util/hash.hh"
#include "nix/store/path-info.hh"

namespace nix {

struct StoreDirConfig;

struct UnkeyedNarInfo : virtual UnkeyedValidPathInfo
{
    std::string url;
    std::string compression;
    std::optional<Hash> fileHash;
    uint64_t fileSize = 0;

    UnkeyedNarInfo(UnkeyedValidPathInfo info)
        : UnkeyedValidPathInfo(std::move(info))
    {
    }

    bool operator==(const UnkeyedNarInfo &) const = default;
    // TODO libc++ 16 (used by darwin) missing `std::optional::operator <=>`, can't do yet
    // auto operator <=>(const NarInfo &) const = default;

    nlohmann::json toJSON(const StoreDirConfig * store, bool includeImpureInfo) const override;
    static UnkeyedNarInfo fromJSON(const StoreDirConfig * store, const nlohmann::json & json);
};

/**
 * Key and the extra NAR fields
 */
struct NarInfo : ValidPathInfo, UnkeyedNarInfo
{
    NarInfo() = delete;

    NarInfo(ValidPathInfo info)
        : UnkeyedValidPathInfo(std::move(static_cast<UnkeyedValidPathInfo &&>(info)))
        // later moves will be partially ignored
        , ValidPathInfo(std::move(info))
        , UnkeyedNarInfo(std::move(info))
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

    std::string to_string(const StoreDirConfig & store) const;
};

} // namespace nix

JSON_IMPL(nix::UnkeyedNarInfo)
