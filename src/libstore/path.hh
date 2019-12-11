#pragma once

#include "rust-ffi.hh"

namespace nix {

/* See path.rs. */
struct StorePath;

extern "C" {
    void ffi_StorePath_drop(void *);
    bool ffi_StorePath_less_than(const StorePath & a, const StorePath & b);
    bool ffi_StorePath_eq(const StorePath & a, const StorePath & b);
    unsigned char * ffi_StorePath_hash_data(const StorePath & p);
}

struct StorePath : rust::Value<3 * sizeof(void *) + 24, ffi_StorePath_drop>
{
    static StorePath make(std::string_view path, std::string_view storeDir);

    static StorePath make(unsigned char hash[20], std::string_view name);

    static StorePath fromBaseName(std::string_view baseName);

    rust::String to_string() const;

    bool operator < (const StorePath & other) const
    {
        return ffi_StorePath_less_than(*this, other);
    }

    bool operator == (const StorePath & other) const
    {
        return ffi_StorePath_eq(*this, other);
    }

    bool operator != (const StorePath & other) const
    {
        return !(*this == other);
    }

    StorePath clone() const;

    /* Check whether a file name ends with the extension for
       derivations. */
    bool isDerivation() const;

    std::string_view name() const;

    unsigned char * hashData() const
    {
        return ffi_StorePath_hash_data(*this);
    }
};

typedef std::set<StorePath> StorePathSet;
typedef std::vector<StorePath> StorePaths;

StorePathSet cloneStorePathSet(const StorePathSet & paths);
StorePathSet storePathsToSet(const StorePaths & paths);

StorePathSet singleton(const StorePath & path);

/* Size of the hash part of store paths, in base-32 characters. */
const size_t storePathHashLen = 32; // i.e. 160 bits

/* Extension of derivations in the Nix store. */
const std::string drvExtension = ".drv";

}

namespace std {

template<> struct hash<nix::StorePath> {
    std::size_t operator()(const nix::StorePath & path) const noexcept
    {
        return * (std::size_t *) path.hashData();
    }
};

}
