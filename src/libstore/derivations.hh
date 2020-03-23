#pragma once

#include "types.hh"
#include "hash.hh"
#include "store-api.hh"

#include <map>


namespace nix {


/* Abstract syntax of derivations. */

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

struct DerivationOutput
{
    StorePath path;
    std::optional<FileSystemHash> hash; /* hash used for expected hash computation */
    DerivationOutput(StorePath && path, std::optional<FileSystemHash> && hash)
        : path(std::move(path))
        , hash(std::move(hash))
    { }
    DerivationOutput(const DerivationOutput &) = default;
    DerivationOutput(DerivationOutput &&) = default;
    DerivationOutput & operator = (const DerivationOutput &) = default;
};

typedef std::map<string, DerivationOutput> DerivationOutputs;

/* For inputs that are sub-derivations, we specify exactly which
   output IDs we are interested in. */
typedef std::map<StorePath, StringSet> DerivationInputs;

typedef std::map<string, string> StringPairs;

struct BasicDerivation
{
    DerivationOutputs outputs; /* keyed on symbolic IDs */
    StorePathSet inputSrcs; /* inputs that are sources */
    string platform;
    Path builder;
    Strings args;
    StringPairs env;

    BasicDerivation() { }
    explicit BasicDerivation(const BasicDerivation & other);
    virtual ~BasicDerivation() { };

    /* Return the path corresponding to the output identifier `id' in
       the given derivation. */
    const StorePath & findOutput(const std::string & id) const;

    bool isBuiltin() const;

    /* Return true iff this is a fixed-output derivation. */
    bool isFixedOutput() const;

    /* Return the output paths of a derivation. */
    StorePathSet outputPaths() const;

};

struct Derivation : BasicDerivation
{
    DerivationInputs inputDrvs; /* inputs that are sub-derivations */

    /* Print a derivation. */
    std::string unparse(const Store & store, bool maskOutputs,
        std::map<std::string, StringSet> * actualInputs = nullptr) const;

    Derivation() { }
    Derivation(Derivation && other) = default;
    explicit Derivation(const Derivation & other);
};


class Store;


/* Write a derivation to the Nix store, and return its path. */
StorePath writeDerivation(ref<Store> store,
    const Derivation & drv, std::string_view name, RepairFlag repair = NoRepair);

/* Read a derivation from a file. */
Derivation readDerivation(const Store & store, const Path & drvPath);

// FIXME: remove
bool isDerivation(const string & fileName);

Hash hashDerivationModulo(Store & store, const Derivation & drv, bool maskOutputs);

/* Memoisation of hashDerivationModulo(). */
typedef std::map<StorePath, Hash> DrvHashes;

extern DrvHashes drvHashes; // FIXME: global, not thread-safe

bool wantOutput(const string & output, const std::set<string> & wanted);

struct Source;
struct Sink;

Source & readDerivation(Source & in, const Store & store, BasicDerivation & drv);
void writeDerivation(Sink & out, const Store & store, const BasicDerivation & drv);

std::string hashPlaceholder(const std::string & outputName);

}
