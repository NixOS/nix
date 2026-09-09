#pragma once
///@file

#include "nix/util/json-impls.hh"
#include "nix/util/json-non-null.hh"
#include "nix/store/path.hh"
#include "nix/store/downstream-placeholder.hh"

#include <cstdint>
#include <optional>

namespace nix {

struct SingleDerivedPath;
struct WorkerSettings;

namespace derivation {

/**
 * Checks (e.g. reference whitelists/blacklists, closure size limits)
 * that apply to derivation outputs.
 *
 * Historically parsed from the legacy environment-variable encoding
 * (see `DerivationATerm::elaborate`).
 */
template<typename Input>
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

template<typename Input>
struct OutputOptions
{
    std::optional<OutputChecks<Input>> checks;

    /**
     * Whether to avoid scanning for references for a given output.
     */
    bool unsafeDiscardReferences = false;

    bool operator==(const OutputOptions &) const = default;
};

/**
 * Options that are for the derivation as a whole, rather than per-output;
 */
template<typename Input>
struct TopOptions
{
    /* Derivation options. Historically these are parsed from special
       environment variables and structured attributes; see
       `DerivationATerm::elaborate`.
       They no longer live in a separate structure because they are
       fields of the derivation like any other. */

    /**
     * Either one set of checks for all outputs, or separate checks
     * per-output.
     */
    std::optional<OutputChecks<Input>> allOutputChecks;

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
    std::map<std::string, std::set<Input>, std::less<>> exportReferencesGraph;

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

    bool operator==(const TopOptions &) const = default;

    bool substitutesAllowed(const WorkerSettings & workerSettings) const;
};

} // namespace derivation

template<typename Input>
struct json_avoids_null<derivation::OutputChecks<Input>> : std::true_type
{};

template<typename Input>
struct json_avoids_null<derivation::TopOptions<Input>> : std::true_type
{};

} // namespace nix

JSON_IMPL(nix::derivation::OutputChecks<nix::StorePath>)
JSON_IMPL(nix::derivation::OutputChecks<nix::SingleDerivedPath>)
JSON_IMPL(nix::derivation::OutputOptions<nix::StorePath>)
JSON_IMPL(nix::derivation::OutputOptions<nix::SingleDerivedPath>)
JSON_IMPL(nix::derivation::TopOptions<nix::StorePath>)
JSON_IMPL(nix::derivation::TopOptions<nix::SingleDerivedPath>)
