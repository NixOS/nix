#pragma once
///@file

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <variant>

#include "types.hh"
#include "json-impls.hh"

namespace nix {

class Store;
struct BasicDerivation;
class ParsedDerivation;

/**
 * This represents all the special options on a `Derivation`.
 *
 * Currently, these options are parsed from the environment variables
 * with the aid of `ParsedDerivation`.
 *
 * The first goal of this data type is to make sure that no other code
 * uses `ParsedDerivation` to ad-hoc parse some additional options. That
 * ensures this data type is up to date and fully correct.
 *
 * The second goal of this data type is to allow an alternative to
 * hackily parsing the options from the environment variables. The ATerm
 * format cannot change, but in alternatives to it (like the JSON
 * format), we have the option of instead storing the options
 * separately. That would be nice to separate concerns, and not make any
 * environment variable names magical.
 */
struct DerivationOptions
{
    struct OutputChecks
    {
        bool ignoreSelfRefs = false;
        std::optional<uint64_t> maxSize, maxClosureSize;

        /**
         * env: allowedReferences
         *
         * A value of `nullopt` indicates that the check is skipped.
         * This means that all references are allowed.
         */
        std::optional<StringSet> allowedReferences;

        /**
         * env: disallowedReferences
         *
         * No needed for `std::optional`, because skipping the check is
         * the same as disallowing the references.
         */
        StringSet disallowedReferences;

        /**
         * env: allowedRequisites
         *
         * See `allowedReferences`
         */
        std::optional<StringSet> allowedRequisites;

        /**
         * env: disallowedRequisites
         *
         * See `disallowedReferences`
         */
        StringSet disallowedRequisites;

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
     * Parse this information from its legacy encoding as part of the
     * environment. This should not be used with nice greenfield formats
     * (e.g. JSON) but is necessary for supporing old formats (e.g.
     * ATerm).
     */
    static DerivationOptions fromParsedDerivation(const ParsedDerivation & parsed, bool shouldWarn = true);

    /**
     * @param drv Must be the same derivation we parsed this from. In
     * the future we'll flip things around so a `BasicDerivation` has
     * `DerivationOptions` instead.
     */
    StringSet getRequiredSystemFeatures(const BasicDerivation & drv) const;

    /**
     * @param drv See note on `getRequiredSystemFeatures`
     */
    bool canBuildLocally(Store & localStore, const BasicDerivation & drv) const;

    /**
     * @param drv See note on `getRequiredSystemFeatures`
     */
    bool willBuildLocally(Store & localStore, const BasicDerivation & drv) const;

    bool substitutesAllowed() const;

    /**
     * @param drv See note on `getRequiredSystemFeatures`
     */
    bool useUidRange(const BasicDerivation & drv) const;
};

};

JSON_IMPL(DerivationOptions);
JSON_IMPL(DerivationOptions::OutputChecks)
