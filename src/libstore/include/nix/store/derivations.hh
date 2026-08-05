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
#include "nix/store/derivation/output.hh"
#include "nix/store/derivation-options.hh"
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
struct WorkerSettings;

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

template<typename Input, typename Out = Output>
struct Derivation;

/**
 * @brief Derivation that depends only on other store objects.
 *
 * This type is what's used in Store::buildDerivation or for resolved derivations
 *
 * @see derivation::tryResolve.
 */
using Basic = Derivation<StorePath>;

/**
 * @brief Derivation that depends on the outputs of other derivations in addition.
 *
 * This type is what's constructed by the evaluator and written to the store in
 * ATerm format.
 */
using Full = Derivation<SingleDerivedPath>;

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
using FullDeferred = Derivation<SingleDerivedPath, Output::Deferred>;

/**
 * @brief `Full`, but statically known to be input-addressed.
 *
 * The outputs are `Output::InputAddressed` rather than the `Output`
 * variant, so "the output paths are computed" is carried in the type.
 */
using FullInputAddressed = Derivation<SingleDerivedPath, Output::InputAddressed>;

template<typename Input, typename Out>
struct OutputWithOptions
{
    Out output;
    OutputOptions<Input> options;

    bool operator==(const OutputWithOptions &) const = default;
};

/**
 * Right-hand side of a VAR=VALUE definition in @see Derivation::env.
 */
struct EnvValue
{
    std::string value;

    /**
     * In non-structured mode, all bindings specified in the derivation
     * go directly via the environment, except those listed in the
     * passAsFile attribute. Those are instead passed as file names
     * pointing to temporary files containing the contents.
     *
     * Note that passAsFile is ignored in structure mode because it's
     * not needed (attributes are not passed through the environment, so
     * there is no size constraint).
     */
    bool passAsFile = false;

    bool operator==(const EnvValue &) const = default;
};

template<typename Input, typename Out>
struct Derivation
{
    std::string name;

    /**
     * keyed on symbolic IDs
     */
    std::map<std::string, OutputWithOptions<Input, Out>, std::less<>> outputs;
    std::set<Input> inputs;
    std::string platform;
    /**
     * Probably should be an absolute path in the path format that `platform` uses
     */
    std::string builder;
    Strings args;
    /**
     * Must not contain the key `__json`, at least in order to serialize to ATerm.
     */
    std::map<std::string, EnvValue, std::less<>> env;
    std::optional<StructuredAttrs> structuredAttrs;

    TopOptions<Input> options;

    bool operator==(const Derivation &) const = default;

    bool isBuiltin() const;

    /**
     * Return the output names of a derivation.
     */
    StringSet outputNames() const;

    static std::string_view nameFromPath(const StorePath & storePath);

    StringSet getRequiredSystemFeatures() const
        requires std::is_same_v<Out, Output>;

    bool useUidRange() const
        requires std::is_same_v<Out, Output>;

    /**
     * Apply string rewrites to the `env`, `args` and `builder`
     * fields.
     */
    void applyRewrites(const StringMap & rewrites);

    /**
     * Return a derivation identical to this one, but with the inputs transformed by `f`.
     *
     * N.B. neither the top-level nor the per-output options are carried
     * over (they may contain `Input`-typed references); they are
     * default-initialized and must be filled in by the caller.
     */
    template<typename F>
    Derivation<typename std::invoke_result_t<F, const std::set<Input> &>::value_type, Out> mapInputs(F f) const
    {
        Derivation<typename std::invoke_result_t<F, const std::set<Input> &>::value_type, Out> res{
            .name = name,
            .inputs = f(inputs),
            .platform = platform,
            .builder = builder,
            .args = args,
            .env = env,
            .structuredAttrs = structuredAttrs,
        };
        for (auto & [outputName, output] : outputs)
            res.outputs.insert_or_assign(
                outputName, typename decltype(res.outputs)::mapped_type{.output = output.output});
        return res;
    }

    /**
     * Return a derivation identical to this one, but with each output
     * transformed by `f`.
     *
     * Only the output *payload* is transformed; the per-output options
     * are `Input`-typed, which does not change here, so they are carried
     * over as they are.
     */
    template<typename F>
    Derivation<Input, std::invoke_result_t<F, const Out &>> mapOutputs(F f) const
    {
        Derivation<Input, std::invoke_result_t<F, const Out &>> res{
            .name = name,
            .inputs = inputs,
            .platform = platform,
            .builder = builder,
            .args = args,
            .env = env,
            .structuredAttrs = structuredAttrs,
            .options = options,
        };
        for (auto & [outputName, output] : outputs)
            res.outputs.insert_or_assign(
                outputName,
                typename decltype(res.outputs)::mapped_type{
                    .output = f(output.output),
                    .options = output.options,
                });
        return res;
    }
};

/**
 * Return true iff this is a fixed-output derivation.
 */
template<typename Input>
Type type(const Derivation<Input, Output> & drv);

/**
 * Calculates the maps that contains all the `Outputs`, but
 * augmented with knowledge of the Store paths they would be written
 * into.
 */
template<typename Input>
OutputsAndOptPaths outputsAndOptPaths(const Derivation<Input, Output> & drv, const StoreDirConfig & store);

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
template<typename Input>
void checkInvariants(const Derivation<Input, Output> & drv, Store & store, const StorePath & drvPath)
{
    checkInvariants(drv, store, readInvalid(store), drvPath);
}

/**
 * Like the above, but instead of reading input derivations from a
 * store, uses a given `ReadDerivation` to get them.
 */
template<typename Input>
void checkInvariants(
    const Derivation<Input, Output> & drv,
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
 * JSON format version for derivation serialization.
 *
 * Used by `nix derivation show` and `nix derivation add`.
 */
enum class JsonFormat : uint64_t {
    /**
     * Legacy format: the derivation options are not first-class, but
     * instead encoded in `env` (and `structuredAttrs`); relatedly,
     * environment variable values are plain strings (the *pass as
     * file* flag being part of the legacy encoding), and outputs
     * carry no inline checks.
     *
     * Decoding this format requires a store, in order to parse the
     * options out of the environment variables.
     */
    V4 = 4,
    /**
     * Current format: first-class `options` field, per-output checks
     * inline in the `outputs` map, and environment variable values
     * that may be objects carrying the *pass as file* flag.
     *
     * This format is "pure": it can be encoded and decoded without
     * reference to any store.
     */
    V5 = 5,
};

/**
 * Convert an integer version number to a `JsonFormat`.
 * Throws Error if the version is not supported.
 */
JsonFormat parseJsonFormat(uint64_t version);

/**
 * Serialize a derivation to JSON in the given format.
 *
 * Note that the legacy `JsonFormat::V4` relies on the derivation
 * options also being encoded in `env` (and `structuredAttrs`), in
 * sync with the first-class fields — as is guaranteed for any
 * derivation read from the store.
 */
template<typename Input>
nlohmann::json toJSON(const Derivation<Input> & drv, JsonFormat format);

/**
 * Parse a derivation from JSON, in any supported format.
 *
 * Unlike the plain JSON decoder, which is store-independent and
 * therefore only supports the current format, this can also decode
 * the legacy `JsonFormat::V4`, using the store directory to parse the
 * derivation options out of their legacy environment-variable
 * encoding.
 *
 * Unlike `parseJsonAndValidate`, this does not fill in output paths
 * or check invariants, and so does not need a full store.
 */
Full fromJSON(
    const StoreDirConfig & store,
    const nlohmann::json & json,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * Parse a derivation from JSON, and also perform various
 * conveniences such as:
 *
 * 1. Filling in output paths in as needed/required.
 *
 * 2. Checking invariants in general.
 *
 * 3. Assisting with the migration from older JSON formats: for
 *    `JsonFormat::V4` input, the derivation options are initialized
 *    from their legacy encoding inside the `env` and
 *    `structuredAttrs`, which is why this function, unlike the plain
 *    JSON decoder (which only accepts the current format), needs a
 *    store.
 *
 * @param store The store to use for path computation and validation
 * @param json The JSON representation of the derivation
 * @return A validated derivation with output paths filled in
 * @throws Error if parsing fails, output paths can't be computed, or validation fails
 */
Full parseJsonAndValidate(
    Store & store,
    const nlohmann::json & json,
    const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

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

template<>
struct json_avoids_null<derivation::EnvValue> : std::true_type
{};

template<typename Input, typename Output>
struct json_avoids_null<derivation::OutputWithOptions<Input, Output>> : std::true_type
{};

} // namespace nix

JSON_IMPL(nix::derivation::EnvValue)

namespace nix {
/**
 * Just to avoid a comma in a macro invocation below.
 */
template<typename Input>
using OutputWithOptionsFor = derivation::OutputWithOptions<Input, derivation::Output>;
} // namespace nix

namespace nlohmann {
template<typename Input>
JSON_IMPL_WITH_XP_FEATURES_INNER(nix::OutputWithOptionsFor<Input>);
template<typename Input>
JSON_IMPL_WITH_XP_FEATURES_INNER(nix::derivation::Derivation<Input>);
} // namespace nlohmann
