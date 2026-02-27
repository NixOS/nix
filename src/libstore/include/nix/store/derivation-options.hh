#pragma once
///@file

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <variant>

#include "nix/util/types.hh"
#include "nix/util/json-impls.hh"
#include "nix/store/store-dir-config.hh"
#include "nix/store/downstream-placeholder.hh"
#include "nix/store/worker-settings.hh"

namespace nix {

struct StoreDirConfig;
struct BasicDerivation;
struct StructuredAttrs;

template<typename V>
struct DerivedPathMap;

/**
 * This represents all the special options on a `Derivation`.
 *
 * Currently, these options are parsed from the environment variables
 * with the aid of `StructuredAttrs`.
 *
 * The first goal of this data type is to make sure that no other code
 * uses `StructuredAttrs` to ad-hoc parse some additional options. That
 * ensures this data type is up to date and fully correct.
 *
 * The second goal of this data type is to allow an alternative to
 * hackily parsing the options from the environment variables. The ATerm
 * format cannot change, but in alternatives to it (like the JSON
 * format), we have the option of instead storing the options
 * separately. That would be nice to separate concerns, and not make any
 * environment variable names magical.
 */
template<typename Input>
struct DerivationOptions
{
    struct OutputChecks
    {
        bool ignoreSelfRefs = false;
        std::optional<uint64_t> maxSize, maxClosureSize;

        using DrvRef = nix::DrvRef<Input>;

        /**
         * env: allowedReferences
         *
         * A value of `nullopt` indicates that the check is skipped.
         * This means that all references are allowed.
         */
        std::optional<std::set<DrvRef>> allowedReferences;

        /**
         * env: disallowedReferences
         *
         * No needed for `std::optional`, because skipping the check is
         * the same as disallowing the references.
         */
        std::set<DrvRef> disallowedReferences;

        /**
         * env: allowedRequisites
         *
         * See `allowedReferences`
         */
        std::optional<std::set<DrvRef>> allowedRequisites;

        /**
         * env: disallowedRequisites
         *
         * See `disallowedReferences`
         */
        std::set<DrvRef> disallowedRequisites;

        bool operator==(const OutputChecks &) const = default;
    };

    /**
     * Either one set of checks for all outputs, or separate checks
     * per-output.
     */
    std::variant<OutputChecks, std::map<std::string, OutputChecks>> outputChecks = OutputChecks{};

    /**
     * Whether to avoid scanning for references for a given output.
     */
    std::map<std::string, bool> unsafeDiscardReferences;

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
    StringSet passAsFile;

    /**
     * The `exportReferencesGraph' feature allows the references graph
     * to be passed to a builder
     *
     * ### Legacy case
     *
     * Given a `name` `pathSet` key-value pair, the references graph of
     * `pathSet` will be stored in a text file `name' in the temporary
     * build directory.  The text files have the format used by
     * `nix-store
     * --register-validity'.  However, the `deriver` fields are left
     *  empty.
     *
     * ### "Structured attributes" case
     *
     * The same information will be put put in the final structured
     * attributes give to the builder. The set of paths in the original JSON
     * is replaced with a list of `PathInfo` in JSON format.
     */
    std::map<std::string, std::set<Input>> exportReferencesGraph;

    /**
     * env: __sandboxProfile
     *
     * Just for Darwin
     */
    std::string additionalSandboxProfile = "";

    /**
     * env: __noChroot
     *
     * Derivation would like to opt out of the sandbox.
     *
     * Builder is free to not respect this wish (because it is
     * insecure) and fail the build instead.
     */
    bool noChroot = false;

    /**
     * env: __impureHostDeps
     */
    StringSet impureHostDeps = {};

    /**
     * env: impureEnvVars
     */
    StringSet impureEnvVars = {};

    /**
     * env: __darwinAllowLocalNetworking
     *
     * Just for Darwin
     */
    bool allowLocalNetworking = false;

    /**
     * env: requiredSystemFeatures
     */
    StringSet requiredSystemFeatures = {};

    /**
     * env: preferLocalBuild
     */
    bool preferLocalBuild = false;

    /**
     * env: allowSubstitutes
     */
    bool allowSubstitutes = true;

    bool operator==(const DerivationOptions &) const = default;

    /**
     * @param drv Must be the same derivation we parsed this from. In
     * the future we'll flip things around so a `BasicDerivation` has
     * `DerivationOptions` instead.
     */
    StringSet getRequiredSystemFeatures(const BasicDerivation & drv) const;

    bool substitutesAllowed(const WorkerSettings & workerSettings) const;

    /**
     * @param drv See note on `getRequiredSystemFeatures`
     */
    bool useUidRange(const BasicDerivation & drv) const;
};

extern template struct DerivationOptions<StorePath>;
extern template struct DerivationOptions<SingleDerivedPath>;

struct DerivationOutput;

/**
 * Parse this information from its legacy encoding as part of the
 * environment. This should not be used with nice greenfield formats
 * (e.g. JSON) but is necessary for supporting old formats (e.g.
 * ATerm).
 */
DerivationOptions<SingleDerivedPath> derivationOptionsFromStructuredAttrs(
    const StoreDirConfig & store,
    const DerivedPathMap<StringSet> & inputDrvs,
    const StringMap & env,
    const StructuredAttrs * parsed,
    bool shouldWarn = true,
    const ExperimentalFeatureSettings & mockXpSettings = experimentalFeatureSettings);

DerivationOptions<StorePath> derivationOptionsFromStructuredAttrs(
    const StoreDirConfig & store,
    const StringMap & env,
    const StructuredAttrs * parsed,
    bool shouldWarn = true,
    const ExperimentalFeatureSettings & mockXpSettings = experimentalFeatureSettings);

/**
 * This is the counterpart of `Derivation::tryResolve`. In particular,
 * it takes the same sort of callback, which is used to reolve
 * non-constant deriving paths.
 *
 * We need this function when resolving a derivation, and we will use
 * this as part of that if/when `Derivation` includes
 * `DerivationOptions`
 */
std::optional<DerivationOptions<StorePath>> tryResolve(
    const DerivationOptions<SingleDerivedPath> & drvOptions,
    fun<std::optional<StorePath>(ref<const SingleDerivedPath> drvPath, const std::string & outputName)>
        queryResolutionChain);

}; // namespace nix

JSON_IMPL(nix::DerivationOptions<nix::StorePath>);
JSON_IMPL(nix::DerivationOptions<nix::SingleDerivedPath>);
JSON_IMPL(nix::DerivationOptions<nix::StorePath>::OutputChecks)
JSON_IMPL(nix::DerivationOptions<nix::SingleDerivedPath>::OutputChecks)
