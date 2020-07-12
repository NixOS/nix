#pragma once

#include "path.hh"
#include "types.hh"
#include "hash.hh"
#include "content-address.hh"

#include <map>


namespace nix {


/* Abstract syntax of derivations. */

struct DerivationOutputInputAddressed
{
    /* Will need to become `std::optional<StorePath>` once input-addressed
       derivations are allowed to depend on cont-addressed derivations */
    StorePath path;
};

struct DerivationOutputFixed
{
    FixedOutputHash hash; /* hash used for expected hash computation */
};

struct DerivationOutputFloating
{
    /* information used for expected hash computation */
    FileIngestionMethod method;
    HashType hashType;
};

struct DerivationOutput
{
    std::variant<
        DerivationOutputInputAddressed,
        DerivationOutputFixed,
        DerivationOutputFloating
    > output;
    std::optional<HashType> hashAlgoOpt(const Store & store) const;
    std::optional<StorePath> pathOpt(const Store & store, std::string_view drvName) const;
    /* DEPRECATED: Remove after CA drvs are fully implemented */
    StorePath path(const Store & store, std::string_view drvName) const {
        auto p = pathOpt(store, drvName);
        if (!p) throw Error("floating content-addressed derivations are not yet implemented");
        return *p;
    }
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
    std::string name;

    BasicDerivation() { }
    virtual ~BasicDerivation() { };

    /* Return the path corresponding to the output identifier `id' in
       the given derivation. */
    const StorePath findOutput(const Store & store, const std::string & id) const;

    bool isBuiltin() const;

    /* Return true iff this is a fixed-output derivation. */
    bool isFixedOutput() const;

    /* Return the output paths of a derivation. */
    StorePathSet outputPaths(const Store & store) const;

    /* Return the output names of a derivation. */
    StringSet outputNames() const;

    static std::string_view nameFromPath(const StorePath & storePath);
};

struct Derivation : BasicDerivation
{
    DerivationInputs inputDrvs; /* inputs that are sub-derivations */

    /* Print a derivation. */
    std::string unparse(const Store & store, bool maskOutputs,
        std::map<std::string, StringSet> * actualInputs = nullptr) const;

    Derivation() { }
};


class Store;

enum RepairFlag : bool { NoRepair = false, Repair = true };

/* Write a derivation to the Nix store, and return its path. */
StorePath writeDerivation(ref<Store> store,
    const Derivation & drv, std::string_view name, RepairFlag repair = NoRepair);

/* Read a derivation from a file. */
Derivation readDerivation(const Store & store, const Path & drvPath, std::string_view name);

// FIXME: remove
bool isDerivation(const string & fileName);

Hash hashDerivationModulo(Store & store, const Derivation & drv, bool maskOutputs);

/* Memoisation of hashDerivationModulo(). */
typedef std::map<StorePath, Hash> DrvHashes;

extern DrvHashes drvHashes; // FIXME: global, not thread-safe

bool wantOutput(const string & output, const std::set<string> & wanted);

struct Source;
struct Sink;

Source & readDerivation(Source & in, const Store & store, BasicDerivation & drv, std::string_view name);
void writeDerivation(Sink & out, const Store & store, const BasicDerivation & drv);

std::string hashPlaceholder(const std::string & outputName);

}
