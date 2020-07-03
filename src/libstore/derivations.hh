#pragma once

#include "path.hh"
#include "types.hh"
#include "hash.hh"
#include "content-address.hh"

#include <map>


namespace nix {


/* Abstract syntax of derivations. */

struct DerivationOutput
{
    StorePath path;
    std::optional<FixedOutputHash> hash; /* hash used for expected hash computation */
};

typedef std::map<string, DerivationOutput, std::less<>> DerivationOutputs;

/* For inputs that are sub-derivations, we specify exactly which
   output IDs we are interested in. */
typedef std::map<StorePath, StringSet, std::less<>> DerivationInputs;

typedef std::map<string, string, std::less<>> StringPairs;

struct BasicDerivation
{
    DerivationOutputs outputs; /* keyed on symbolic IDs */
    StorePathSet inputSrcs; /* inputs that are sources */
    string platform;
    Path builder;
    Strings args;
    StringPairs env;

    BasicDerivation() { }
    virtual ~BasicDerivation() { };

    /* Return the path corresponding to the output identifier `id' in
       the given derivation. */
    const StorePath & findOutput(std::string_view id) const;

    bool isBuiltin() const;

    /* Return true iff this is a fixed-output derivation. */
    bool isFixedOutput() const;

    /* Return the output paths of a derivation. */
    StorePathSet outputPaths() const;

    /* Return the output names of a derivation. */
    StringSet outputNames() const;
};

struct Derivation : BasicDerivation
{
    DerivationInputs inputDrvs; /* inputs that are sub-derivations */

    /* Print a derivation. */
    std::string unparse(const Store & store, bool maskOutputs,
        std::map<std::string, StringSet, std::less<>> * actualInputs = nullptr) const;

    Derivation() { }
};


class Store;

enum RepairFlag : bool { NoRepair = false, Repair = true };

/* Write a derivation to the Nix store, and return its path. */
StorePath writeDerivation(ref<Store> store,
    const Derivation & drv, std::string_view name, RepairFlag repair = NoRepair);

/* Read a derivation from a file. */
Derivation readDerivation(const Store & store, PathView drvPath);

// FIXME: remove
bool isDerivation(std::string_view fileName);

Hash hashDerivationModulo(Store & store, const Derivation & drv, bool maskOutputs);

/* Memoisation of hashDerivationModulo(). */
typedef std::map<StorePath, Hash, std::less<>> DrvHashes;

extern DrvHashes drvHashes; // FIXME: global, not thread-safe

bool wantOutput(std::string_view output, const std::set<string, std::less<>> & wanted);

struct Source;
struct Sink;

Source & readDerivation(Source & in, const Store & store, BasicDerivation & drv);
void writeDerivation(Sink & out, const Store & store, const BasicDerivation & drv);

std::string hashPlaceholder(std::string_view outputName);

}
