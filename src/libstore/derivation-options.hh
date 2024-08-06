#pragma once
///@file

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include "types.hh"
#include <variant>

namespace nix {

class AdditionalAttributes
{
    typedef StringPairs Env;

    std::variant<Env, nlohmann::json> attrs;

    std::optional<std::string> getStringAttr(const std::string & name) const;
    bool getBoolAttr(const std::string & name, bool def = false) const;
    std::optional<Strings> getStringsAttr(const std::string & name) const;
};

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
        std::optional<Strings> allowedReferences = std::nullopt;

        /**
         * env: disallowedReferences
         *
         * A value of `nullopt` indicates that the check is skipped.
         * This means that there are no disallowed references.
         */
        std::optional<Strings> disallowedReferences = std::nullopt;

        /**
         * env: allowedRequisites
         *
         * See `allowedReferences`
         */
        std::optional<Strings> allowedRequisites = std::nullopt;

        /**
         * env: disallowedRequisites
         *
         * See `disallowedReferences`
         */
        std::optional<Strings> disallowedRequisites = std::nullopt;

        bool operator==(const OutputChecks &) const = default;
        auto operator<=>(const OutputChecks &) const = default;
    };

    std::map<std::string, OutputChecks> checksPerOutput;

    OutputChecks checksAllOutputs;

    AdditionalAttributes attrs;

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
    Strings impureHostDeps = {};

    /**
     * env: impureEnvVars
     */
    Strings impureEnvVars = {};

    /**
     * env: __darwinAllowLocalNetworking
     *
     * Just for Darwin
     */
    bool allowLocalNetworking = false;

    /**
     * env: requiredSystemFeatures
     */
    Strings requiredSystemFeatures = {};

    /**
     * env: preferLocalBuild
     */
    bool preferLocalBuild = false;

    /**
     * env: allowSubstitutes
     */
    bool allowSubstitutes = true;

    bool operator==(const DerivationOptions &) const = default;
    auto operator<=>(const DerivationOptions &) const = default;

    /**
     * Parse this information from its legacy encoding as part of the
     * environment. This should not be used with nice greenfield formats
     * (e.g. JSON) but is necessary for supporing old formats (e.g.
     * ATerm).
     */
    static DerivationOptions fromEnv(const StringPairs & env, bool shouldWarn = true);
};

};
