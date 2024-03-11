#pragma once
///@file

#include "path.hh"
#include "types.hh"
#include "hash.hh"
#include "content-address.hh"
#include "repair-flag.hh"
#include "derived-path-map.hh"
#include "sync.hh"
#include "comparator.hh"
#include "variant-wrapper.hh"

#include <map>
#include <variant>


namespace nix {

struct StoreDirConfig;

/* Abstract syntax of derivations. */

/**
 * A single output of a BasicDerivation (and Derivation).
 */
struct DerivationOutput
{
    /**
     * The traditional non-fixed-output derivation type.
     */
    struct InputAddressed
    {
        StorePath path;

        GENERATE_CMP(InputAddressed, me->path);
    };

    /**
     * Fixed-output derivations, whose output paths are content
     * addressed according to that fixed output.
     */
    struct CAFixed
    {
        /**
         * Method and hash used for expected hash computation.
         *
         * References are not allowed by fiat.
         */
        ContentAddress ca;

        /**
         * Return the \ref StorePath "store path" corresponding to this output
         *
         * @param drvName The name of the derivation this is an output of, without the `.drv`.
         * @param outputName The name of this output.
         */
        StorePath path(const StoreDirConfig & store, std::string_view drvName, OutputNameView outputName) const;

        GENERATE_CMP(CAFixed, me->ca);
    };

    /**
     * Floating-output derivations, whose output paths are content
     * addressed, but not fixed, and so are dynamically calculated from
     * whatever the output ends up being.
     * */
    struct CAFloating
    {
        /**
         * How the file system objects will be serialized for hashing
         */
        ContentAddressMethod method;

        /**
         * How the serialization will be hashed
         */
        HashAlgorithm hashAlgo;

        GENERATE_CMP(CAFloating, me->method, me->hashAlgo);
    };

    /**
     * Input-addressed output which depends on a (CA) derivation whose hash
     * isn't known yet.
     */
    struct Deferred {
        GENERATE_CMP(Deferred);
    };

    /**
     * Impure output which is moved to a content-addressed location (like
     * CAFloating) but isn't registered as a realization.
     */
    struct Impure
    {
        /**
         * How the file system objects will be serialized for hashing
         */
        ContentAddressMethod method;

        /**
         * How the serialization will be hashed
         */
        HashAlgorithm hashAlgo;

        GENERATE_CMP(Impure, me->method, me->hashAlgo);
    };

    typedef std::variant<
        InputAddressed,
        CAFixed,
        CAFloating,
        Deferred,
        Impure
    > Raw;

    Raw raw;

    GENERATE_CMP(DerivationOutput, me->raw);

    MAKE_WRAPPER_CONSTRUCTOR(DerivationOutput);

    /**
     * Force choosing a variant
     */
    DerivationOutput() = delete;

    /**
     * \note when you use this function you should make sure that you're
     * passing the right derivation name. When in doubt, you should use
     * the safer interface provided by
     * BasicDerivation::outputsAndOptPaths
     */
    std::optional<StorePath> path(const StoreDirConfig & store, std::string_view drvName, OutputNameView outputName) const;

    nlohmann::json toJSON(
        const StoreDirConfig & store,
        std::string_view drvName,
        OutputNameView outputName) const;
    /**
     * @param xpSettings Stop-gap to avoid globals during unit tests.
     */
    static DerivationOutput fromJSON(
        const StoreDirConfig & store,
        std::string_view drvName,
        OutputNameView outputName,
        const nlohmann::json & json,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);
};

typedef std::map<std::string, DerivationOutput> DerivationOutputs;

/**
 * These are analogues to the previous DerivationOutputs data type,
 * but they also contains, for each output, the (optional) store
 * path in which it would be written. To calculate values of these
 * types, see the corresponding functions in BasicDerivation.
 */
typedef std::map<std::string, std::pair<DerivationOutput, std::optional<StorePath>>>
  DerivationOutputsAndOptPaths;

/**
 * For inputs that are sub-derivations, we specify exactly which
 * output IDs we are interested in.
 */
typedef std::map<StorePath, StringSet> DerivationInputs;

struct DerivationType {
    /**
     * Input-addressed derivation types
     */
    struct InputAddressed {
        /**
         * True iff the derivation type can't be determined statically,
         * for instance because it (transitively) depends on a content-addressed
         * derivation.
        */
        bool deferred;

        GENERATE_CMP(InputAddressed, me->deferred);
    };

    /**
     * Content-addressed derivation types
     */
    struct ContentAddressed {
        /**
         * Whether the derivation should be built safely inside a sandbox.
         */
        bool sandboxed;
        /**
         * Whether the derivation's outputs' content-addresses are "fixed"
         * or "floating".
         *
         *  - Fixed: content-addresses are written down as part of the
         *    derivation itself. If the outputs don't end up matching the
         *    build fails.
         *
         *  - Floating: content-addresses are not written down, we do not
         *    know them until we perform the build.
         */
        bool fixed;

        GENERATE_CMP(ContentAddressed, me->sandboxed, me->fixed);
    };

    /**
     * Impure derivation type
     *
     * This is similar at buil-time to the content addressed, not standboxed, not fixed
     * type, but has some restrictions on its usage.
     */
    struct Impure {
        GENERATE_CMP(Impure);
    };

    typedef std::variant<
        InputAddressed,
        ContentAddressed,
        Impure
    > Raw;

    Raw raw;

    GENERATE_CMP(DerivationType, me->raw);

    MAKE_WRAPPER_CONSTRUCTOR(DerivationType);

    /**
     * Force choosing a variant
     */
    DerivationType() = delete;

    /**
     * Do the outputs of the derivation have paths calculated from their
     * content, or from the derivation itself?
     */
    bool isCA() const;

    /**
     * Is the content of the outputs fixed <em>a priori</em> via a hash?
     * Never true for non-CA derivations.
     */
    bool isFixed() const;

    /**
     * Whether the derivation is fully sandboxed. If false, the sandbox
     * is opened up, e.g. the derivation has access to the network. Note
     * that whether or not we actually sandbox the derivation is
     * controlled separately. Always true for non-CA derivations.
     */
    bool isSandboxed() const;

    /**
     * Whether the derivation is expected to produce a different result
     * every time, and therefore it needs to be rebuilt every time. This is
     * only true for derivations that have the attribute '__impure =
     * true'.
     *
     * Non-impure derivations can still behave impurely, to the degree permitted
     * by the sandbox. Hence why this method isn't `isPure`: impure derivations
     * are not the negation of pure derivations. Purity can not be ascertained
     * except by rather heavy tools.
     */
    bool isImpure() const;

    /**
     * Does the derivation knows its own output paths?
     * Only true when there's no floating-ca derivation involved in the
     * closure, or if fixed output.
     */
    bool hasKnownOutputPaths() const;
};

struct BasicDerivation
{
    /**
     * keyed on symbolic IDs
     */
    DerivationOutputs outputs;
    /**
     * inputs that are sources
     */
    StorePathSet inputSrcs;
    std::string platform;
    Path builder;
    Strings args;
    StringPairs env;
    std::string name;

    BasicDerivation() = default;
    virtual ~BasicDerivation() { };

    bool isBuiltin() const;

    /**
     * Return true iff this is a fixed-output derivation.
     */
    DerivationType type() const;

    /**
     * Return the output names of a derivation.
     */
    StringSet outputNames() const;

    /**
     * Calculates the maps that contains all the DerivationOutputs, but
     * augmented with knowledge of the Store paths they would be written
     * into.
     */
    DerivationOutputsAndOptPaths outputsAndOptPaths(const StoreDirConfig & store) const;

    static std::string_view nameFromPath(const StorePath & storePath);

    GENERATE_CMP(BasicDerivation,
        me->outputs,
        me->inputSrcs,
        me->platform,
        me->builder,
        me->args,
        me->env,
        me->name);
};

class Store;

struct Derivation : BasicDerivation
{
    /**
     * inputs that are sub-derivations
     */
    DerivedPathMap<std::set<OutputName>> inputDrvs;

    /**
     * Print a derivation.
     */
    std::string unparse(const StoreDirConfig & store, bool maskOutputs,
        DerivedPathMap<StringSet>::ChildNode::Map * actualInputs = nullptr) const;

    /**
     * Return the underlying basic derivation but with these changes:
     *
     * 1. Input drvs are emptied, but the outputs of them that were used
     *    are added directly to input sources.
     *
     * 2. Input placeholders are replaced with realized input store
     *    paths.
     */
    std::optional<BasicDerivation> tryResolve(Store & store, Store * evalStore = nullptr) const;

    /**
     * Like the above, but instead of querying the Nix database for
     * realisations, uses a given mapping from input derivation paths +
     * output names to actual output store paths.
     */
    std::optional<BasicDerivation> tryResolve(
        Store & store,
        const std::map<std::pair<StorePath, std::string>, StorePath> & inputDrvOutputs) const;

    /**
     * Check that the derivation is valid and does not present any
     * illegal states.
     *
     * This is mainly a matter of checking the outputs, where our C++
     * representation supports all sorts of combinations we do not yet
     * allow.
     */
    void checkInvariants(Store & store, const StorePath & drvPath) const;

    Derivation() = default;
    Derivation(const BasicDerivation & bd) : BasicDerivation(bd) { }
    Derivation(BasicDerivation && bd) : BasicDerivation(std::move(bd)) { }

    nlohmann::json toJSON(const StoreDirConfig & store) const;
    static Derivation fromJSON(
        const StoreDirConfig & store,
        const nlohmann::json & json,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

    GENERATE_CMP(Derivation,
        static_cast<const BasicDerivation &>(*me),
        me->inputDrvs);
};


class Store;

/**
 * Write a derivation to the Nix store, and return its path.
 */
StorePath writeDerivation(Store & store,
    const Derivation & drv,
    RepairFlag repair = NoRepair,
    bool readOnly = false);

/**
 * Read a derivation from a file.
 */
Derivation parseDerivation(
    const StoreDirConfig & store,
    std::string && s,
    std::string_view name,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * \todo Remove.
 *
 * Use Path::isDerivation instead.
 */
bool isDerivation(std::string_view fileName);

/**
 * Calculate the name that will be used for the store path for this
 * output.
 *
 * This is usually <drv-name>-<output-name>, but is just <drv-name> when
 * the output name is "out".
 */
std::string outputPathName(std::string_view drvName, OutputNameView outputName);


/**
 * The hashes modulo of a derivation.
 *
 * Each output is given a hash, although in practice only the content-addressed
 * derivations (fixed-output or not) will have a different hash for each
 * output.
 */
struct DrvHash {
    /**
     * Map from output names to hashes
     */
    std::map<std::string, Hash> hashes;

    enum struct Kind : bool {
        /**
         * Statically determined derivations.
         * This hash will be directly used to compute the output paths
         */
        Regular,

        /**
         * Floating-output derivations (and their reverse dependencies).
         */
        Deferred,
    };

    /**
     * The kind of derivation this is, simplified for just "derivation hash
     * modulo" purposes.
     */
    Kind kind;
};

void operator |= (DrvHash::Kind & self, const DrvHash::Kind & other) noexcept;

/**
 * Returns hashes with the details of fixed-output subderivations
 * expunged.
 *
 * A fixed-output derivation is a derivation whose outputs have a
 * specified content hash and hash algorithm. (Currently they must have
 * exactly one output (`out`), which is specified using the `outputHash`
 * and `outputHashAlgo` attributes, but the algorithm doesn't assume
 * this.) We don't want changes to such derivations to propagate upwards
 * through the dependency graph, changing output paths everywhere.
 *
 * For instance, if we change the url in a call to the `fetchurl`
 * function, we do not want to rebuild everything depending on it---after
 * all, (the hash of) the file being downloaded is unchanged.  So the
 * *output paths* should not change. On the other hand, the *derivation
 * paths* should change to reflect the new dependency graph.
 *
 * For fixed-output derivations, this returns a map from the name of
 * each output to its hash, unique up to the output's contents.
 *
 * For regular derivations, it returns a single hash of the derivation
 * ATerm, after subderivations have been likewise expunged from that
 * derivation.
 */
DrvHash hashDerivationModulo(Store & store, const Derivation & drv, bool maskOutputs);

/**
 * Return a map associating each output to a hash that uniquely identifies its
 * derivation (modulo the self-references).
 *
 * \todo What is the Hash in this map?
 */
std::map<std::string, Hash> staticOutputHashes(Store & store, const Derivation & drv);

/**
 * Memoisation of hashDerivationModulo().
 */
typedef std::map<StorePath, DrvHash> DrvHashes;

// FIXME: global, though at least thread-safe.
extern Sync<DrvHashes> drvHashes;

struct Source;
struct Sink;

Source & readDerivation(Source & in, const StoreDirConfig & store, BasicDerivation & drv, std::string_view name);
void writeDerivation(Sink & out, const StoreDirConfig & store, const BasicDerivation & drv);

/**
 * This creates an opaque and almost certainly unique string
 * deterministically from the output name.
 *
 * It is used as a placeholder to allow derivations to refer to their
 * own outputs without needing to use the hash of a derivation in
 * itself, making the hash near-impossible to calculate.
 */
std::string hashPlaceholder(const OutputNameView outputName);

extern const Hash impureOutputHash;

}
