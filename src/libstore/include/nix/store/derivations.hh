#pragma once
///@file

#include "nix/store/path.hh"
#include "nix/store/derivation/output.hh"
#include "nix/util/types.hh"
#include "nix/util/hash.hh"
#include "nix/store/content-address.hh"
#include "nix/util/repair-flag.hh"
#include "nix/store/derived-path-map.hh"
#include "nix/store/parsed-derivations.hh"
#include "nix/util/sync.hh"
#include "nix/util/variant-wrapper.hh"

#include <boost/unordered/concurrent_flat_map_fwd.hpp>
#include <variant>

namespace nix {

/**
 * String to include in requiredSystemFeatures to enable builder-rpc-v0
 */
static constexpr std::string_view drvFeatureBuilderRpcV0 = "builder-rpc-v0";

struct StoreDirConfig;

/**
 * For inputs that are sub-derivations, we specify exactly which
 * output IDs we are interested in.
 */
typedef std::map<StorePath, StringSet> DerivationInputs;

struct DerivationType
{
    /**
     * Input-addressed derivation types
     */
    struct InputAddressed
    {
        /**
         * True iff the derivation type can't be determined statically,
         * for instance because it (transitively) depends on a content-addressed
         * derivation.
         */
        bool deferred;

        bool operator==(const InputAddressed &) const = default;
        auto operator<=>(const InputAddressed &) const = default;
    };

    /**
     * Content-addressing derivation types
     */
    struct ContentAddressed
    {
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

        bool operator==(const ContentAddressed &) const = default;
        auto operator<=>(const ContentAddressed &) const = default;
    };

    /**
     * Impure derivation type
     *
     * This is similar at build-time to the content addressed, not standboxed, not fixed
     * type, but has some restrictions on its usage.
     */
    struct Impure
    {
        bool operator==(const Impure &) const = default;
        auto operator<=>(const Impure &) const = default;
    };

    typedef std::variant<InputAddressed, ContentAddressed, Impure> Raw;

    Raw raw;

    bool operator==(const DerivationType &) const = default;
    auto operator<=>(const DerivationType &) const = default;

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

template<typename Inputs, typename Output = DerivationOutput>
struct DerivationT;

using BasicDerivation = DerivationT<StorePathSet>;
using Derivation = DerivationT<std::set<SingleDerivedPath>>;

template<typename Inputs, typename Output>
struct DerivationT
{
    /**
     * keyed on symbolic IDs
     */
    DerivationOutputs<Output> outputs;
    Inputs inputs;
    std::string platform;
    /**
     * Probably should be an absolute path in the path format that `platform` uses
     */
    std::string builder;
    Strings args;
    /**
     * Must not contain the key `__json`, at least in order to serialize to ATerm.
     */
    StringPairs env;
    std::optional<StructuredAttrs> structuredAttrs;

    std::string name;

    bool operator==(const DerivationT &) const = default;

    bool isBuiltin() const
        requires std::is_same_v<Output, DerivationOutput>;

    /**
     * Return true iff this is a fixed-output derivation.
     */
    DerivationType type() const
        requires std::is_same_v<Output, DerivationOutput>;

    /**
     * Return the output names of a derivation.
     */
    StringSet outputNames() const
        requires std::is_same_v<Output, DerivationOutput>;

    /**
     * Calculates the maps that contains all the DerivationOutputs, but
     * augmented with knowledge of the Store paths they would be written
     * into.
     */
    DerivationOutputsAndOptPaths outputsAndOptPaths(const StoreDirConfig & store) const
        requires std::is_same_v<Output, DerivationOutput>;

    static std::string_view nameFromPath(const StorePath & storePath);

    /**
     * Apply string rewrites to the `env`, `args` and `builder`
     * fields.
     */
    void applyRewrites(const StringMap & rewrites)
        requires std::is_same_v<Output, DerivationOutput>;

    /**
     * Print a derivation (only meaningful for full Derivation).
     */
    std::string unparse(const StoreDirConfig & store) const
        requires std::is_same_v<Inputs, std::set<SingleDerivedPath>> && std::is_same_v<Output, DerivationOutput>;

    /**
     * Determine whether this derivation should be resolved before building.
     *
     * Resolution is needed when:
     * - Input-addressed derivations are deferred (depend on CA derivations)
     * - Content-addressed derivations have input drvs and are either:
     *   - Floating (non-fixed), which must always be resolved
     *   - Fixed, which can optionally be resolved when ca-derivations is enabled
     * - Impure derivations always need resolution
     * - Any input derivations have outputs from dynamic derivations
     */
    bool shouldResolve() const
        requires std::is_same_v<Inputs, std::set<SingleDerivedPath>> && std::is_same_v<Output, DerivationOutput>;

    /**
     * Return the underlying basic derivation but with these changes:
     *
     * 1. Input drvs are emptied, but the outputs of them that were used
     *    are added directly to input sources.
     *
     * 2. Input placeholders are replaced with realized input store
     *    paths.
     */
    std::optional<BasicDerivation> tryResolve(Store & store, Store * evalStore = nullptr) const
        requires std::is_same_v<Inputs, std::set<SingleDerivedPath>> && std::is_same_v<Output, DerivationOutput>;

    /**
     * Like the above, but instead of querying the Nix database for
     * realisations, uses a given mapping from input derivation paths +
     * output names to actual output store paths.
     */
    std::optional<BasicDerivation> tryResolve(
        Store & store,
        fun<std::optional<StorePath>(ref<const SingleDerivedPath> drvPath, const std::string & outputName)>
            queryResolutionChain) const
        requires std::is_same_v<Inputs, std::set<SingleDerivedPath>> && std::is_same_v<Output, DerivationOutput>;

    /**
     * Convert a BasicDerivation to a full Derivation.
     * The resulting Derivation has empty inputDrvs since BasicDerivation
     * is already resolved.
     */
    Derivation unresolve() const
        requires std::is_same_v<Inputs, StorePathSet> && std::is_same_v<Output, DerivationOutput>;

    /**
     * Return a derivation identical to this one, but with the inputs transformed by `f`.
     */
    template<typename F>
    DerivationT<std::invoke_result_t<F, const Inputs &>, Output> mapInputs(F f) const
    {
        return {
            .outputs = outputs,
            .inputs = f(inputs),
            .platform = platform,
            .builder = builder,
            .args = args,
            .env = env,
            .structuredAttrs = structuredAttrs,
            .name = name,
        };
    }

    /**
     * Check that the derivation is valid and does not present any
     * illegal states.
     *
     * This is mainly a matter of checking the outputs, where our C++
     * representation supports all sorts of combinations we do not yet
     * allow.
     *
     * This overload does not validate the derivation name or add path
     * context to errors. Use this when you don't have a `StorePath` or
     * when you want to handle error context yourself.
     *
     * @param store The store to use for validation
     */
    void checkInvariants(Store & store) const
        requires std::is_same_v<Output, DerivationOutput>;

    /**
     * This overload does everything the base `checkInvariants` does,
     * but also validates that the derivation name matches the path, and
     * improves any error messages that occur using the derivation path.
     *
     * @param store The store to use for validation
     * @param drvPath The path to this derivation
     */
    void checkInvariants(Store & store, const StorePath & drvPath) const
        requires std::is_same_v<Output, DerivationOutput>;

    /**
     * Fill in output paths as needed.
     *
     * For input-addressed derivations (ready or deferred), it computes
     * the derivation hash modulo and based on the result:
     *
     * - If `Regular`: converts `Deferred` outputs to `InputAddressed`,
     *   and ensures all `InputAddressed` outputs (whether preexisting
     *   or newly computed) have the right computed paths. Likewise
     *   defines (if absent or the empty string) or checks (if
     *   preexisting and non-empty) environment variables for each
     *   output with their path.
     *
     * - If `Deferred`: converts `InputAddressed` to `Deferred`.
     *
     * Also for fixed-output content-addressed derivations, likewise
     * updates output paths in env vars.
     *
     * @param store The store to use for path computation
     * @param drvName The derivation name (without .drv extension)
     */
    void fillInOutputPaths(Store & store)
        requires std::is_same_v<Inputs, std::set<SingleDerivedPath>> && std::is_same_v<Output, DerivationOutput>;

    /**
     * Parse a derivation from JSON, and also perform various
     * conveniences such as:
     *
     * 1. Filling in output paths in as needed/required.
     *
     * 2. Checking invariants in general.
     *
     * In the future it might also do things like:
     *
     * - assist with the migration from older JSON formats.
     *
     * - (a somewhat example of the above) initialize
     *   `DerivationOptions` from their traditional encoding inside the
     *   `env` and `structuredAttrs`.
     *
     * @param store The store to use for path computation and validation
     * @param json The JSON representation of the derivation
     * @return A validated derivation with output paths filled in
     * @throws Error if parsing fails, output paths can't be computed, or validation fails
     */
    static Derivation parseJsonAndValidate(Store & store, const nlohmann::json & json)
        requires std::is_same_v<Inputs, std::set<SingleDerivedPath>> && std::is_same_v<Output, DerivationOutput>;
};

class Store;

template<>
std::string Derivation::unparse(const StoreDirConfig & store) const;
template<>
bool Derivation::shouldResolve() const;
template<>
std::optional<BasicDerivation> Derivation::tryResolve(Store & store, Store * evalStore) const;
template<>
std::optional<BasicDerivation> Derivation::tryResolve(
    Store & store,
    fun<std::optional<StorePath>(ref<const SingleDerivedPath> drvPath, const std::string & outputName)>
        queryResolutionChain) const;
template<>
void Derivation::fillInOutputPaths(Store & store);
template<>
Derivation Derivation::parseJsonAndValidate(Store & store, const nlohmann::json & json);
template<>
Derivation DerivationT<StorePathSet>::unresolve() const;
template<>
void DerivationT<StorePathSet>::checkInvariants(Store & store) const;
template<>
void Derivation::checkInvariants(Store & store) const;

/**
 * Compute the store path that would be used for a derivation without writing it.
 *
 * This is a pure computation based on the derivation content and store directory.
 */
StorePath computeStorePath(const StoreDirConfig & store, const Derivation & drv);

/**
 * \todo Remove.
 *
 * Use Path::isDerivation instead.
 */
bool isDerivation(std::string_view fileName);

/**
 * If a derivation is input addressed and doesn't yet have its input
 * addressed (is deferred) try using `hashInputDerivationModulo`.
 *
 * Does nothing if not deferred input-addressed, or
 * `hashInputDerivationModulo` indicates it is missing inputs' output paths
 * and is not yet ready (and must stay deferred).
 */
void resolveInputAddressed(Store & store, Derivation & drv);

/**
 * This creates an opaque and almost certainly unique string
 * deterministically from the output name.
 *
 * It is used as a placeholder to allow derivations to refer to their
 * own outputs without needing to use the hash of a derivation in
 * itself, making the hash near-impossible to calculate.
 */
std::string hashPlaceholder(const OutputNameView outputName);

/**
 * The expected JSON version for derivation serialization.
 * Used by `nix derivation show` and `nix derivation add`.
 */
constexpr unsigned expectedJsonVersionDerivation = 4;

} // namespace nix


namespace nlohmann {
template<typename Inputs>
JSON_IMPL_WITH_XP_FEATURES_INNER(nix::DerivationT<Inputs>);
} // namespace nlohmann
