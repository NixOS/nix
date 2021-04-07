#pragma once

#include "path.hh"
#include "types.hh"
#include "hash.hh"
#include "content-address.hh"
#include "derived-path-map.hh"
#include "sync.hh"

#include <map>
#include <variant>


namespace nix {


/* Abstract syntax of derivations. */

/* The traditional non-fixed-output derivation type. */
struct DerivationOutputInputAddressed
{
    StorePath path;
};

/* Fixed-output derivations, whose output paths are content addressed
   according to that fixed output. */
struct DerivationOutputCAFixed
{
    ContentAddressWithReferences ca; /* hash and refs used for validating output */
    StorePath path(const Store & store, std::string_view drvName, std::string_view outputName) const;
};

/* Floating-output derivations, whose output paths are content addressed, but
   not fixed, and so are dynamically calculated from whatever the output ends
   up being. */
struct DerivationOutputCAFloating
{
    /* information used for expected hash computation */
    ContentAddressMethod method;
    HashType hashType;
};

/* Input-addressed output which depends on a (CA) derivation whose hash isn't
 * known atm
 */
struct DerivationOutputDeferred {};

struct DerivationOutput
{
    std::variant<
        DerivationOutputInputAddressed,
        DerivationOutputCAFixed,
        DerivationOutputCAFloating,
        DerivationOutputDeferred
    > output;

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
typedef std::map<string, std::pair<DerivationOutput, std::optional<StorePath>>>
  DerivationOutputsAndOptPaths;

/* For inputs that are sub-derivations, we specify exactly which
   output IDs we are interested in. */
typedef std::map<StorePath, StringSet> DerivationInputs;

typedef std::map<string, string> StringPairs;

enum struct DerivationType : uint8_t {
    InputAddressed,
    DeferredInputAddressed,
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

/* Does the derivation knows its own output paths?
 * Only true when there's no floating-ca derivation involved in the closure.
 */
bool derivationHasKnownOutputPaths(DerivationType);

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
    DerivedPathMap<StringSet> inputDrvs; /* inputs that are sub-derivations */

    /* Print a derivation. */
    std::string unparse(const Store & store, bool maskOutputs,
        DerivedPathMap<StringSet>::ChildMap * actualInputs = nullptr) const;

    /* Return the underlying basic derivation but with these changes:

       1. Input drvs are emptied, but the outputs of them that were used are
          added directly to input sources.

       2. Input placeholders are replaced with realized input store paths. */
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

struct DrvHash {
    Hash hash;
    bool isDeferred;
};

typedef std::variant<
    // Regular normalized derivation hash, and whether it was deferred (because
    // an ancestor derivation is a floating content addressed derivation).
    DrvHash,
    // Fixed-output derivation hashes
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

/*
   Return a map associating each output to a hash that uniquely identifies its
   derivation (modulo the self-references).
 */
std::map<std::string, Hash> staticOutputHashes(Store& store, const Derivation& drv);
std::map<std::string, DrvHash> staticOutputHashesWithDeferral(Store& store, const Derivation& drv);

/* Memoisation of hashDerivationModulo(). */
typedef std::map<StorePath, DrvHashModulo> DrvHashes;

// FIXME: global, though at least thread-safe.
extern Sync<DrvHashes> drvHashes;

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

class DownstreamPlaceholder
{
    Hash hash;

public:
    /* This creates an opaque and almost certainly unique string
       deterministically from a derivation path and output name.
    
       It is used as a placeholder to allow derivations to refer to
       content-addressed paths whose content --- and thus the path
       themselves --- isn't yet known. This occurs when a derivation has a
       dependency which is a CA derivation. */
    std::string render() const;

    static DownstreamPlaceholder parse(std::string_view);

    // For CA derivations
    DownstreamPlaceholder(const StorePath & drvPath, std::string_view outputName);

    // For computed derivations
    DownstreamPlaceholder(const DownstreamPlaceholder & placeholder, std::string_view outputName);

private:
    DownstreamPlaceholder(Hash && h) : hash(std::move(h)) {}
    static inline Hash worker1(const StorePath & drvPath, std::string_view outputName);
    static inline Hash worker2(const DownstreamPlaceholder & placeholder, std::string_view outputName);
};

}
