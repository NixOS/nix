#pragma once

#include "types.hh"

namespace nix {

class Store;
struct Hash;

class StorePath
{
    std::string baseName;

    StorePath(const StorePath & path)
        : baseName(path.baseName)
    { }

public:

    /* Size of the hash part of store paths, in base-32 characters. */
    constexpr static size_t HashLen = 32; // i.e. 160 bits

    StorePath() = delete;

    StorePath(std::string_view baseName);

    StorePath(const Hash & hash, std::string_view name);

    StorePath(StorePath && path)
        : baseName(std::move(path.baseName))
    { }

    StorePath & operator = (StorePath && path)
    {
        baseName = std::move(path.baseName);
        return *this;
    }

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

    StorePath clone() const
    {
        return StorePath(*this);
    }

    /* Check whether a file name ends with the extension for
       derivations. */
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
};

typedef std::set<StorePath> StorePathSet;
typedef std::vector<StorePath> StorePaths;

StorePathSet cloneStorePathSet(const StorePathSet & paths);
StorePathSet storePathsToSet(const StorePaths & paths);

StorePathSet singleton(const StorePath & path);

/* Extension of derivations in the Nix store. */
const std::string drvExtension = ".drv";

enum struct FileIngestionMethod : uint8_t {
    Flat = false,
    Recursive = true
};

struct StorePathWithOutputs
{
    StorePath path;
    std::set<std::string> outputs;

    StorePathWithOutputs(const StorePath & path, const std::set<std::string> & outputs = {})
        : path(path.clone()), outputs(outputs)
    { }

    StorePathWithOutputs(StorePath && path, std::set<std::string> && outputs)
        : path(std::move(path)), outputs(std::move(outputs))
    { }

    StorePathWithOutputs(const StorePathWithOutputs & other)
        : path(other.path.clone()), outputs(other.outputs)
    { }

    std::string to_string(const Store & store) const;
};

std::pair<std::string_view, StringSet> parsePathWithOutputs(std::string_view s);

}

namespace std {

template<> struct hash<nix::StorePath> {
    std::size_t operator()(const nix::StorePath & path) const noexcept
    {
        return * (std::size_t *) path.to_string().data();
    }
};

}
