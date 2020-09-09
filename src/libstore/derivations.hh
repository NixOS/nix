#pragma once

#include "path.hh"
#include "types.hh"
#include "hash.hh"
#include "content-address.hh"
#include "sync.hh"

#include <map>
#include <variant>


namespace nix {


/* Abstract syntax of derivations. */

/* The traditional non-fixed-output derivation type. */
struct DerivationOutputInputAddressed
{
    /* Will need to become `std::optional<StorePath>` once input-addressed
       derivations are allowed to depend on cont-addressed derivations */
    StorePath path;
};

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

struct DerivationOutput
{
    std::variant<
        DerivationOutputInputAddressed,
        DerivationOutputCAFixed,
        DerivationOutputCAFloating
    > output;
    std::optional<HashType> hashAlgoOpt(const Store & store) const;
    /* Note, when you use this function you should make sure that you're passing
       the right derivation name. When in doubt, you should use the safer
       interface provided by BasicDerivation::outputsAndOptPaths */
    std::optional<StorePath> path(const Store & store, std::string_view drvName, std::string_view outputName) const;
};

typedef std::map<string, DerivationOutput> DerivationOutputs;

/* These are analogues to the previous DerivationOutputs data type, but they
   also contains, for each output, the (optional) store path in which it would
   be written. To calculate values of these types, see the corresponding
   functions in BasicDerivation */
typedef std::map<string, std::pair<DerivationOutput, StorePath>>
  DerivationOutputsAndPaths;
typedef std::map<string, std::pair<DerivationOutput, std::optional<StorePath>>>
  DerivationOutputsAndOptPaths;

/* For inputs that are sub-derivations, we specify exactly which
   output IDs we are interested in. */
typedef std::map<StorePath, StringSet> DerivationInputs;

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

struct BasicDerivation
{
    DerivationOutputs outputs; /* keyed on symbolic IDs */
    StorePathSet inputSrcs; /* inputs that are sources */
    string platform;
    Path builder;
    Strings args;
    StringPairs env;
    std::string name;

    BasicDerivation() = default;
    virtual ~BasicDerivation() { };

    bool isBuiltin() const;

    /* Return true iff this is a fixed-output derivation. */
    DerivationType type() const;

    /* Return the output names of a derivation. */
    StringSet outputNames() const;

    /* Calculates the maps that contains all the DerivationOutputs, but
       augmented with knowledge of the Store paths they would be written
       into. */
    DerivationOutputsAndOptPaths outputsAndOptPaths(const Store & store) const;

    static std::string_view nameFromPath(const StorePath & storePath);
};

struct Derivation : BasicDerivation
{
    DerivationInputs inputDrvs; /* inputs that are sub-derivations */

    /* Print a derivation. */
    std::string unparse(const Store & store, bool maskOutputs,
        std::map<std::string, StringSet> * actualInputs = nullptr) const;

    /* Return the underlying basic derivation but with

       1. input drv outputs moved to input sources.

       2. placeholders replaced with realized input store paths. */
    std::optional<BasicDerivation> tryResolve(Store & store);

    Derivation() = default;
    Derivation(const BasicDerivation & bd) : BasicDerivation(bd) { }
    Derivation(BasicDerivation && bd) : BasicDerivation(std::move(bd)) { }
};


class Store;

enum RepairFlag : bool { NoRepair = false, Repair = true };

/* Write a derivation to the Nix store, and return its path. */
StorePath writeDerivation(Store & store,
    const Derivation & drv,
    RepairFlag repair = NoRepair,
    bool readOnly = false);

/* Read a derivation from a file. */
Derivation parseDerivation(const Store & store, std::string && s, std::string_view name);

// FIXME: remove
bool isDerivation(const string & fileName);

/* Calculate the name that will be used for the store path for this
   output.

   This is usually <drv-name>-<output-name>, but is just <drv-name> when
   the output name is "out". */
std::string outputPathName(std::string_view drvName, std::string_view outputName);

// known CA drv's output hashes, current just for fixed-output derivations
// whose output hashes are always known since they are fixed up-front.
typedef std::map<std::string, Hash> CaOutputHashes;

typedef std::variant<
    Hash, // regular DRV normalized hash
    CaOutputHashes
> DrvHashModulo;

/* Returns hashes with the details of fixed-output subderivations
   expunged.

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

   For fixed-output derivations, this returns a map from the name of
   each output to its hash, unique up to the output's contents.

   For regular derivations, it returns a single hash of the derivation
   ATerm, after subderivations have been likewise expunged from that
   derivation.
 */
DrvHashModulo hashDerivationModulo(Store & store, const Derivation & drv, bool maskOutputs);

/* Memoisation of hashDerivationModulo(). */
typedef std::map<StorePath, DrvHashModulo> DrvHashes;

extern DrvHashes drvHashes; // FIXME: global, not thread-safe

/* Memoisation of `readDerivation(..).resove()`. */
typedef std::map<
    StorePath,
    std::optional<StorePath>
> DrvPathResolutions;

// FIXME: global, though at least thread-safe.
// FIXME: arguably overlaps with hashDerivationModulo memo table.
extern Sync<DrvPathResolutions> drvPathResolutions;

bool wantOutput(const string & output, const std::set<string> & wanted);

struct Source;
struct Sink;

Source & readDerivation(Source & in, const Store & store, BasicDerivation & drv, std::string_view name);
void writeDerivation(Sink & out, const Store & store, const BasicDerivation & drv);

/* This creates an opaque and almost certainly unique string
   deterministically from the output name.

   It is used as a placeholder to allow derivations to refer to their
   own outputs without needing to use the hash of a derivation in
   itself, making the hash near-impossible to calculate. */
std::string hashPlaceholder(const std::string & outputName);

/* This creates an opaque and almost certainly unique string
   deterministically from a derivation path and output name.

   It is used as a placeholder to allow derivations to refer to
   content-addressed paths whose content --- and thus the path
   themselves --- isn't yet known. This occurs when a derivation has a
   dependency which is a CA derivation. */
std::string downstreamPlaceholder(const Store & store, const StorePath & drvPath, std::string_view outputName);

}
