#pragma once

#include "hash.hh"

namespace nix {

enum struct FileIngestionMethod : uint8_t {
    Flat = false,
    Recursive = true
};

/// Pair of a hash, and how the file system was ingested
struct FileSystemHash {
    FileIngestionMethod method;
    Hash hash;
    FileSystemHash(FileIngestionMethod method, Hash hash)
        : method(std::move(method))
        , hash(std::move(hash))
    { }
    FileSystemHash(const FileSystemHash &) = default;
    FileSystemHash(FileSystemHash &&) = default;
    FileSystemHash & operator = (const FileSystemHash &) = default;
    std::string printMethodAlgo() const;
};

/* Compute the prefix to the hash algorithm which indicates how the files were
   ingested. */
std::string makeFileIngestionPrefix(const FileIngestionMethod m);

/* Compute the content-addressability assertion (ValidPathInfo::ca)
   for paths created by makeFixedOutputPath() / addToStore(). */
std::string makeFixedOutputCA(FileIngestionMethod method, const Hash & hash);

}
