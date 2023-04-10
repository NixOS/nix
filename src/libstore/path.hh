#pragma once
///@file

#include <string_view>

#include "types.hh"

namespace nix {

struct Hash;

/**
 * \ref StorePath "Store path" is the fundamental reference type of Nix.
 * A store paths refers to a Store object.
 *
 * See glossary.html#gloss-store-path for more information on a
 * conceptual level.
 */
class StorePath
{
    std::string baseName;

public:

    /**
     * Size of the hash part of store paths, in base-32 characters.
     */
    constexpr static size_t HashLen = 32; // i.e. 160 bits

    constexpr static size_t MaxPathLen = 211;

    StorePath() = delete;

    StorePath(std::string_view baseName);

    StorePath(const Hash & hash, std::string_view name);

    std::string_view to_string() const
    {
        return baseName;
    }

    bool operator < (const StorePath & other) const
    {
        return baseName < other.baseName;
    }

    bool operator == (const StorePath & other) const
    {
        return baseName == other.baseName;
    }

    bool operator != (const StorePath & other) const
    {
        return baseName != other.baseName;
    }

    /**
     * Check whether a file name ends with the extension for derivations.
     */
    bool isDerivation() const;

    std::string_view name() const
    {
        return std::string_view(baseName).substr(HashLen + 1);
    }

    std::string_view hashPart() const
    {
        return std::string_view(baseName).substr(0, HashLen);
    }

    static StorePath dummy;

    static StorePath random(std::string_view name);
};

typedef std::set<StorePath> StorePathSet;
typedef std::vector<StorePath> StorePaths;

/**
 * The file extension of \ref Derivation derivations when serialized
 * into store objects.
 */
const std::string drvExtension = ".drv";

}

namespace std {

template<> struct hash<nix::StorePath> {
    std::size_t operator()(const nix::StorePath & path) const noexcept
    {
        return * (std::size_t *) path.to_string().data();
    }
};

}
