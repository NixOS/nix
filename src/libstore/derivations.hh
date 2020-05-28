#pragma once

#include <map>
#include <variant>

#include "path.hh"
#include "types.hh"
#include "hash.hh"


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

template<typename Path>
struct DerivationOutputT
{
    Path path;
    std::optional<FileSystemHash> hash; /* hash used for expected hash computation */

    DerivationOutputT(Path path, std::optional<FileSystemHash> hash)
        : path(std::move(path))
        , hash(std::move(hash))
    { }
    DerivationOutputT(const DerivationOutputT<Path> &) = default;
    DerivationOutputT(DerivationOutputT<Path> &&) = default;
    DerivationOutputT & operator = (const DerivationOutputT<Path> &) = default;
    void parseHashInfo(FileIngestionMethod & recursive, Hash & hash) const;
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
    explicit BasicDerivationT(const BasicDerivationT<OutputPath> & other);
    virtual ~BasicDerivationT() { };

    /* Return the path corresponding to the output identifier `id' in
       the given derivation. */
    const OutputPath & findOutput(const std::string & id) const;

    bool isBuiltin() const;

    /* Return true iff this is a fixed-output derivation. */
    bool isFixedOutput() const;

};

typedef BasicDerivationT<StorePath> BasicDerivation;

/* Return the output paths of a derivation. */
std::set<StorePath> outputPaths(const BasicDerivation &);

struct NoPath: std::monostate {
    constexpr NoPath clone() const {
        return *this;
    };
};

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
    DerivationT(DerivationT<InputDrvPath, OutputPath> && other) = default;
    DerivationT(const BasicDerivationT<OutputPath> & other);
    explicit DerivationT(const DerivationT<InputDrvPath, OutputPath> & other);
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


// TODO dedup some with `Store::makeFixedOutputPath` after DerivationOutput
// doesn't just contain strings but actual `Hash` and recursive vs flat enum.

/// Print the symbolic output path if it is fixed output
std::optional<std::string> printLogicalOutput(
    Store & store,
    const DerivationOutputT<StorePath> & output);
std::optional<std::string> printLogicalOutput(
    Store & store,
    const DerivationOutputT<NoPath> & output,
    const std::string & drvName);

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

/* Reduce a derivation down to a resolved normal form if it is regular, or
   symbolic output path if it is fixed output. */
template<typename InputDrvPath>
std::variant<DerivationT<Hash, StorePath>, string> derivationModuloOrOutput(
    Store & store,
    const DerivationT<InputDrvPath, StorePath> & drv);
template<typename InputDrvPath>
std::variant<DerivationT<Hash, NoPath>, string> derivationModuloOrOutput(
    Store & store,
    const DerivationT<InputDrvPath, NoPath> & drv,
    const std::string & drvName);

/// Turn the output of derivationModuloOrOutput into a plain hash.
template<typename OutPath>
Hash hashDerivationOrPseudo(
    Store & store,
    typename std::variant<DerivationT<Hash, OutPath>, std::string> drvOrPseudo);

/* Hash a resolved normal form derivation. */
template<typename OutPath>
Hash hashDerivation(
    Store & store,
    const DerivationT<Hash, OutPath> & drv);

/* Compute a "baked" derivation, made from the which additionally contains the
   outputs paths created from the hash of the initial one. */
template<typename InputDrvPath>
DerivationT<InputDrvPath, StorePath> bakeDerivationPaths(
    Store & store,
    const DerivationT<InputDrvPath, NoPath> & drv,
    const std::string & drvName);

/* Opposite of bakeDerivationPaths */
template<typename InputDrvPath>
DerivationT<InputDrvPath, NoPath> stripDerivationPaths(
    Store & store,
    const DerivationT<InputDrvPath, StorePath> & drv);

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
