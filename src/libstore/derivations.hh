#pragma once

#include <map>
#include <variant>

#include "path.hh"
#include "types.hh"
#include "hash.hh"
#include "content-address.hh"


namespace nix {


/* Abstract syntax of derivations. */

template<typename Path>
struct DerivationOutputT
{
    Path path;
    std::optional<FixedOutputHash> hash; /* hash used for expected hash computation */
};

typedef DerivationOutputT<StorePath> DerivationOutput;

typedef std::map<string, DerivationOutput> DerivationOutputs;

template<typename Path>
using DerivationOutputsT = std::map<string, DerivationOutputT<Path>>;

typedef DerivationOutputsT<StorePath> DerivationOutputs;

typedef std::map<string, string> StringPairs;

template<typename OutputPath>
struct BasicDerivationT
{
    DerivationOutputsT<OutputPath> outputs; /* keyed on symbolic IDs */
    StorePathSet inputSrcs; /* inputs that are sources */
    string platform;
    Path builder;
    Strings args;
    StringPairs env;

    BasicDerivationT() { }
    virtual ~BasicDerivationT() { };

    /* Return the path corresponding to the output identifier `id' in
       the given derivation. */
    const OutputPath & findOutput(const std::string & id) const;

    bool isBuiltin() const;

    /* Return true iff this is a fixed-output derivation. */
    bool isFixedOutput() const;

    /* Return the output names of a derivation. */
    StringSet outputNames() const;
};

typedef BasicDerivationT<StorePath> BasicDerivation;

/* Return the output paths of a derivation. */
std::set<StorePath> outputPaths(const BasicDerivation &);

struct NoPath : std::monostate {};

/* For inputs that are sub-derivations, we specify exactly which
   output IDs we are interested in. */
template<typename Path>
using DerivationInputsT = std::map<Path, StringSet>;

typedef DerivationInputsT<StorePath> DerivationInputs;

template<typename InputDrvPath, typename OutputPath>
struct DerivationT : BasicDerivationT<OutputPath>
{
    DerivationInputsT<InputDrvPath> inputDrvs; /* inputs that are sub-derivations */

    /* Print a derivation. */
    std::string unparse(const Store & store, bool maskOutputs,
        std::map<std::string, StringSet> * actualInputs = nullptr) const;

    DerivationT() { }
};

typedef DerivationT<StorePath, StorePath> Derivation;

class Store;

enum RepairFlag : bool { NoRepair = false, Repair = true };

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

namespace std {
template <> struct hash<nix::NoPath>;
}
