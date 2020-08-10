#pragma once

#include <map>
#include <variant>

#include "path.hh"
#include "types.hh"
#include "hash.hh"
#include "content-address.hh"


namespace nix {


/* Abstract syntax of derivations. */

/* The traditional non-fixed-output derivation type. */
template<typename Path>
struct DerivationOutputInputAddressedT
{
    Path path;
};

typedef DerivationOutputInputAddressedT<StorePath> DerivationOutputInputAddressed;

/* Fixed-output derivations, whose output paths are content addressed
   according to that fixed output. */
struct DerivationOutputCAFixed
{
    FixedOutputHash hash; /* hash used for expected hash computation */
    StorePath path(const Store & store, std::string_view drvName, std::string_view outputName) const;
};

/* Floating-output derivations, whose output paths are content addressed, but
   not fixed, and so are dynamically calculated from whatever the output ends
   up being. */
struct DerivationOutputCAFloating
{
    /* information used for expected hash computation */
    FileIngestionMethod method;
    HashType hashType;
};

template<typename Path>
struct DerivationOutputT
{
    std::variant<
        DerivationOutputInputAddressedT<Path>,
        DerivationOutputCAFixed,
        DerivationOutputCAFloating
    > output;
    std::optional<HashType> hashAlgoOpt(const Store & store) const;
    std::optional<StorePath> pathOpt(const Store & store, std::string_view drvName) const;
};

typedef DerivationOutputT<StorePath> DerivationOutput;

template<typename Path>
using DerivationOutputsT = std::map<string, DerivationOutputT<Path>>;

typedef DerivationOutputsT<StorePath> DerivationOutputs;

typedef std::map<string, string> StringPairs;

enum struct DerivationType : uint8_t {
    InputAddressed,
    CAFixed,
    CAFloating,
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

template<typename OutputPath>
struct BasicDerivationT
{
    DerivationOutputsT<OutputPath> outputs; /* keyed on symbolic IDs */
    StorePathSet inputSrcs; /* inputs that are sources */
    string platform;
    Path builder;
    Strings args;
    StringPairs env;
    std::string name;

    BasicDerivationT() { }
    virtual ~BasicDerivationT() { };

    bool isBuiltin() const;

    /* Return true iff this is a fixed-output derivation. */
    DerivationType type() const;

    /* Return the output names of a derivation. */
    StringSet outputNames() const;

    static std::string_view nameFromPath(const StorePath & storePath);
};

typedef BasicDerivationT<StorePath> BasicDerivation;

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
    std::string unparse(const Store & store) const;

    DerivationT() { }
    DerivationT(const BasicDerivationT<OutputPath> & other);
};

typedef DerivationT<StorePath, StorePath> Derivation;

class Store;

enum RepairFlag : bool { NoRepair = false, Repair = true };

/* Write a derivation to the Nix store, and return its path. */
StorePath writeDerivation(ref<Store> store,
    const Derivation & drv, RepairFlag repair = NoRepair);

/* Read a derivation from a file. */
Derivation readDerivation(const Store & store, const Path & drvPath, std::string_view name);

// FIXME: remove
bool isDerivation(const string & fileName);

std::string outputPathName(std::string_view drvName, std::string_view outputName);

/* About all these *modulo* functions.

   A fixed-output derivation is a derivation whose outputs have a
   specified content hash and hash algorithm. (Currently they must have
   exactly one output (`out'), which is specified using the `outputHash'
   and `outputHashAlgo' attributes, but the algorithm doesn't assume
   this.) We don't want changes to such derivations to propagate upwards
   through the dependency graph, changing output paths everywhere.

   For instance, if we change the url in a call to the `fetchurl'
   function, we do not want to rebuild everything depending on it---after
   all, (the hash of) the file being downloaded is unchanged.  So the
   *output paths* should not change. On the other hand, the *derivation
   paths* should change to reflect the new dependency graph.

   For fixed-output derivations, these return a map from the name of
   each output to its hash, unique up to the output's contents.

   For regular derivations, they return the derivation
   ATerm, with subderivations having been likewise expunged.
 */

// known CA drv's output hashes, current just for fixed-output derivations
// whose output hashes are always known since they are fixed up-front.
typedef std::map<std::string, Hash> CaOutputHashes;

/// Reduce a derivation down to a resolved normal form
template<typename OutPath>
DerivationT<Hash, OutPath> derivationModulo(
    Store & store,
    const DerivationT<StorePath, OutPath> & drv);

/// Identity function, but useful to be called from polymorphic code.
template<typename OutPath>
DerivationT<Hash, OutPath> derivationModulo(
    Store & store,
    const DerivationT<Hash, OutPath> & drv);

/* If the derivation is fixed output, transform its (fixed) output hashes into
   a form useful by derivation or modulo. */
template<typename InputDrvPath, typename OutPath>
std::optional<CaOutputHashes> outputHashesForModuloIfFixed(
    Store & store,
    const DerivationT<InputDrvPath, OutPath> & drv);

typedef std::variant<
    Hash, // regular DRV normalized hash
    CaOutputHashes
> DrvHashModulo;

/* Hash a derivation. */
template<typename InputDrvPath, typename OutPath>
Hash hashDerivation(
    Store & store,
    const DerivationT<InputDrvPath, OutPath> & drv);

/* Compute a "baked" derivation, made from the which additionally contains the
   outputs paths created from the hash of the initial one. */
template<typename InputDrvPath>
DerivationT<InputDrvPath, StorePath> bakeDerivationPaths(
    Store & store,
    const DerivationT<InputDrvPath, NoPath> & drv);

/* Opposite of bakeDerivationPaths */
template<typename InputDrvPath>
DerivationT<InputDrvPath, NoPath> stripDerivationPaths(
    Store & store,
    const DerivationT<InputDrvPath, StorePath> & drv);

/* Memoisation of hashDerivationModulo(). */
typedef std::map<StorePath, DrvHashModulo> DrvHashes;

extern DrvHashes drvHashes; // FIXME: global, not thread-safe

bool wantOutput(const string & output, const std::set<string> & wanted);

struct Source;
struct Sink;

Source & readDerivation(Source & in, const Store & store, BasicDerivation & drv, std::string_view name);
void writeDerivation(Sink & out, const Store & store, const BasicDerivation & drv);

std::string hashPlaceholder(const std::string & outputName);

StorePath downstreamPlaceholder(const Store & store, const StorePath & drvPath, std::string_view outputName);

}

namespace std {
template <> struct hash<nix::NoPath>;
}
