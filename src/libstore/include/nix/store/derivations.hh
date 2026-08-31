#pragma once
///@file

#include "nix/store/path.hh"
#include "nix/util/types.hh"
#include "nix/util/hash.hh"
#include "nix/store/content-address.hh"
#include "nix/util/repair-flag.hh"
#include "nix/store/derivation/output.hh"
#include "nix/store/derived-path-map.hh"
#include "nix/store/parsed-derivations.hh"
#include "nix/util/sync.hh"
#include "nix/util/variant-wrapper.hh"
#include "nix/util/fun.hh"

#include <boost/unordered/concurrent_flat_map_fwd.hpp>
#include <variant>

namespace nix {

/**
 * String to include in requiredSystemFeatures to enable builder-rpc-v0
 */
static constexpr std::string_view drvFeatureBuilderRpcV0 = "builder-rpc-v0";

struct StoreDirConfig;
class Store;

/* Abstract syntax of derivations. */

namespace derivation {

/**
 * For inputs that are sub-derivations, we specify exactly which
 * output IDs we are interested in.
 */
typedef std::map<StorePath, StringSet> Inputs;

struct Type
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

    bool operator==(const Type &) const = default;
    auto operator<=>(const Type &) const = default;

    MAKE_WRAPPER_CONSTRUCTOR(Type);

    /**
     * Force choosing a variant
     */
    Type() = delete;

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

template<typename Inputs, typename Out = Output>
struct Derivation;

/**
 * @brief Derivation that depends only on other store objects.
 *
 * This type is what's used in Store::buildDerivation or for resolved derivations
 *
 * @see derivation::tryResolve.
 */
using Basic = Derivation<StorePathSet>;

/**
 * @brief Derivation that depends on the outputs of other derivations in addition.
 *
 * This type is what's constructed by the evaluator and written to the store in
 * ATerm format.
 */
using Full = Derivation<std::set<SingleDerivedPath>>;

/**
 * How to get a derivation's value from its store path.
 *
 * Several computations over derivations --- filling in output paths,
 * checking invariants, hashing modulo --- need to recurse into input
 * derivations, but that is *all* they need a store for; everything else
 * they touch is `StoreDirConfig`, for printing paths. Taking just this
 * much lets them be used (and tested) without a real store, and makes
 * it evident from the signature that nothing else is queried.
 */
using ReadDerivation = fun<Full(const StorePath & drvPath)>;

/**
 * `Store::readInvalidDerivation` as a `ReadDerivation`, for callers
 * that do have a whole store to hand.
 */
ReadDerivation readInvalid(Store & store);

/**
 * @brief `Full`, but statically known to have no output paths yet.
 *
 * The outputs are `Output::Deferred` rather than the `Output` variant,
 * so "we have not computed the output paths" is carried in the type.
 *
 * @see fillInOutputPaths, which turns this into a `FullInputAddressed`.
 */
using FullDeferred = Derivation<std::set<SingleDerivedPath>, Output::Deferred>;

/**
 * @brief `Full`, but statically known to be input-addressed.
 *
 * The outputs are `Output::InputAddressed` rather than the `Output`
 * variant, so "the output paths are computed" is carried in the type.
 */
using FullInputAddressed = Derivation<std::set<SingleDerivedPath>, Output::InputAddressed>;

template<typename Inputs, typename Out>
struct Derivation
{
    /**
     * keyed on symbolic IDs
     */
    Outputs<Out> outputs;
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

    bool operator==(const Derivation &) const = default;

    bool isBuiltin() const;

    /**
     * Return the output names of a derivation.
     */
    StringSet outputNames() const;

    static std::string_view nameFromPath(const StorePath & storePath);

    /**
     * Apply string rewrites to the `env`, `args` and `builder`
     * fields.
     */
    void applyRewrites(const StringMap & rewrites);

    /**
     * Return a derivation identical to this one, but with the inputs transformed by `f`.
     */
    template<typename F>
    Derivation<std::invoke_result_t<F, const Inputs &>, Out> mapInputs(F f) const
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
     * Return a derivation identical to this one, but with each output
     * transformed by `f`.
     */
    template<typename F>
    Derivation<Inputs, std::invoke_result_t<F, const Out &>> mapOutputs(F f) const
    {
        Outputs<std::invoke_result_t<F, const Out &>> newOutputs;
        for (const auto & [name, output] : outputs)
            newOutputs.insert_or_assign(name, f(output));
        return {
            .outputs = std::move(newOutputs),
            .inputs = inputs,
            .platform = platform,
            .builder = builder,
            .args = args,
            .env = env,
            .structuredAttrs = structuredAttrs,
            .name = name,
        };
    }
};

/**
 * Return true iff this is a fixed-output derivation.
 */
template<typename Inputs>
Type type(const Derivation<Inputs, Output> & drv);

/**
 * Calculates the maps that contains all the `Outputs`, but
 * augmented with knowledge of the Store paths they would be written
 * into.
 */
template<typename Inputs>
OutputsAndOptPaths outputsAndOptPaths(const Derivation<Inputs, Output> & drv, const StoreDirConfig & store);

/**
 * Does the derivation have a dependency on the output of a dynamic
 * derivation?
 *
 * In other words, does it depend on the output of a derivation that is
 * itself an output of a derivation? This corresponds to a dependency
 * that is an inductive derived path with more than one layer of
 * `DerivedPath::Built`.
 */
bool hasDynamicDrvDep(const std::set<SingleDerivedPath> & inputs);

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
void checkInvariants(const Basic & drv, const StoreDirConfig & store);
void checkInvariants(const Full & drv, Store & store);

/**
 * Like the above, but instead of reading input derivations from a
 * store, uses a given `ReadDerivation` to get them.
 */
void checkInvariants(const Full & drv, const StoreDirConfig & store, ReadDerivation readDerivation);

/**
 * This overload does everything the base `checkInvariants` does,
 * but also validates that the derivation name matches the path, and
 * improves any error messages that occur using the derivation path.
 *
 * @param store The store to use for validation
 * @param drvPath The path to this derivation
 */
template<typename Inputs>
void checkInvariants(const Derivation<Inputs, Output> & drv, Store & store, const StorePath & drvPath)
{
    checkInvariants(drv, store, readInvalid(store), drvPath);
}

/**
 * Like the above, but instead of reading input derivations from a
 * store, uses a given `ReadDerivation` to get them.
 */
template<typename Inputs>
void checkInvariants(
    const Derivation<Inputs, Output> & drv,
    const StoreDirConfig & store,
    auto && readDerivation,
    const StorePath & drvPath);

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
 */
void fillInOutputPaths(Basic & drv, const StoreDirConfig & store);
void fillInOutputPaths(Full & drv, Store & store);

/**
 * Like the above, but instead of reading input derivations from a
 * store, uses a given `ReadDerivation` to get them.
 */
void fillInOutputPaths(Full & drv, const StoreDirConfig & store, ReadDerivation readDerivation);

/**
 * Functional, statically-typed variant of the above, for a derivation
 * all of whose outputs are known to be `Deferred`.
 *
 * Rather than mutating in place --- which is only possible because
 * `Output` is a variant able to hold either alternative --- this
 * consumes its argument and returns a derivation whose outputs are
 * statically `InputAddressed`, so the "outputs are filled in now" fact
 * is carried in the type. Everything but the (small) outputs is moved
 * through, so this is no more expensive than the mutating version.
 *
 * Returns `std::nullopt` if there is no input address to fill in yet,
 * i.e. when the derivation (transitively) depends on a floating
 * content-addressing derivation. That is the case `Deferred` exists
 * for, and it is precisely the case this function cannot represent in
 * its return type.
 */
std::optional<FullInputAddressed>
fillInOutputPaths(FullDeferred drv, const StoreDirConfig & store, ReadDerivation readDerivation);

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
Full parseJsonAndValidate(Store & store, const nlohmann::json & json);

} // namespace derivation

using BasicDerivation = derivation::Basic;
using Derivation = derivation::Full;

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
 * Calculate the name that will be used for the store path for this
 * output.
 *
 * This is usually <drv-name>-<output-name>, but is just <drv-name> when
 * the output name is "out".
 */
std::string outputPathName(std::string_view drvName, OutputNameView outputName);

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
JSON_IMPL_WITH_XP_FEATURES_INNER(nix::derivation::Derivation<Inputs>);
} // namespace nlohmann
