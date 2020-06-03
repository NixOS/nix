#pragma once

#include "types.hh"
#include "hash.hh"
#include "store-api.hh"

#include <map>


namespace nix {


/* Abstract syntax of derivations. */

struct DerivationOutput
{
    StorePath path;
    std::string hashAlgo; /* hash used for expected hash computation */
    std::string hash; /* expected hash, may be null */
    DerivationOutput(StorePath && path, std::string && hashAlgo, std::string && hash)
        : path(std::move(path))
        , hashAlgo(std::move(hashAlgo))
        , hash(std::move(hash))
    { }
    void parseHashType(FileIngestionMethod & recursive, HashType & hashType) const;
    void parseHashInfo(FileIngestionMethod & recursive, Hash & hash) const;
};

typedef std::map<string, DerivationOutput> DerivationOutputs;

/* For inputs that are sub-derivations, we specify exactly which
   output IDs we are interested in. */
typedef std::map<StorePath, StringSet> DerivationInputs;

typedef std::map<string, string> StringPairs;

enum struct DerivationType : uint8_t {
    Regular,
    CAFixed,
};

/* Do the outputs of the derivation have paths calculated from their content,
   or from the derivation itself? */
bool derivationIsCA(DerivationType);

/* Is the content of the outputs fixed a-priori via a hash? Never true for
   non-CA derivations. */
bool derivationIsFixed(DerivationType);

/* Is the derivation impure and needs to access non-deterministic resources, or
   pure and can be sandboxed? Note that whether or not we actually sandbox the
   derivation is controlled separately. Never true for non-CA derivations. */
bool derivationIsImpure(DerivationType);

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
    DerivationType type() const;

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
