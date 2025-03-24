#include "derivation-options.hh"
#include "json-utils.hh"
#include "parsed-derivations.hh"
#include "types.hh"
#include "util.hh"
#include <optional>
#include <string>
#include <variant>

namespace nix {

using OutputChecks = DerivationOptions::OutputChecks;

using OutputChecksVariant = std::variant<OutputChecks, std::map<std::string, OutputChecks>>;

DerivationOptions DerivationOptions::fromParsedDerivation(const ParsedDerivation & parsed, bool shouldWarn)
{
    DerivationOptions defaults = {};

    auto structuredAttrs = parsed.structuredAttrs.get();

    if (shouldWarn && structuredAttrs) {
        if (get(*structuredAttrs, "allowedReferences")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'allowedReferences'; use 'outputChecks' instead");
        }
        if (get(*structuredAttrs, "allowedRequisites")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'allowedRequisites'; use 'outputChecks' instead");
        }
        if (get(*structuredAttrs, "disallowedRequisites")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'disallowedRequisites'; use 'outputChecks' instead");
        }
        if (get(*structuredAttrs, "disallowedReferences")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'disallowedReferences'; use 'outputChecks' instead");
        }
        if (get(*structuredAttrs, "maxSize")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'maxSize'; use 'outputChecks' instead");
        }
        if (get(*structuredAttrs, "maxClosureSize")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'maxClosureSize'; use 'outputChecks' instead");
        }
    }

    return {
        .outputChecks = [&]() -> OutputChecksVariant {
            if (auto structuredAttrs = parsed.structuredAttrs.get()) {
                std::map<std::string, OutputChecks> res;
                if (auto outputChecks = get(*structuredAttrs, "outputChecks")) {
                    for (auto & [outputName, output] : getObject(*outputChecks)) {
                        OutputChecks checks;

                        if (auto maxSize = get(output, "maxSize"))
                            checks.maxSize = maxSize->get<uint64_t>();

                        if (auto maxClosureSize = get(output, "maxClosureSize"))
                            checks.maxClosureSize = maxClosureSize->get<uint64_t>();

                        auto get_ = [&](const std::string & name) -> std::optional<StringSet> {
                            if (auto i = get(output, name)) {
                                StringSet res;
                                for (auto j = i->begin(); j != i->end(); ++j) {
                                    if (!j->is_string())
                                        throw Error("attribute '%s' must be a list of strings", name);
                                    res.insert(j->get<std::string>());
                                }
                                checks.disallowedRequisites = res;
                                return res;
                            }
                            return {};
                        };

                        checks.allowedReferences = get_("allowedReferences");
                        checks.allowedRequisites = get_("allowedRequisites");
                        checks.disallowedReferences = get_("disallowedReferences").value_or(StringSet{});
                        checks.disallowedRequisites = get_("disallowedRequisites").value_or(StringSet{});
                        ;

                        res.insert_or_assign(outputName, std::move(checks));
                    }
                }
                return res;
            } else {
                return OutputChecks{
                    // legacy non-structured-attributes case
                    .ignoreSelfRefs = true,
                    .allowedReferences = parsed.getStringSetAttr("allowedReferences"),
                    .disallowedReferences = parsed.getStringSetAttr("disallowedReferences").value_or(StringSet{}),
                    .allowedRequisites = parsed.getStringSetAttr("allowedRequisites"),
                    .disallowedRequisites = parsed.getStringSetAttr("disallowedRequisites").value_or(StringSet{}),
                };
            }
        }(),
        .unsafeDiscardReferences =
            [&] {
                std::map<std::string, bool> res;

                if (auto structuredAttrs = parsed.structuredAttrs.get()) {
                    if (auto udr = get(*structuredAttrs, "unsafeDiscardReferences")) {
                        for (auto & [outputName, output] : getObject(*udr)) {
                            if (!output.is_boolean())
                                throw Error("attribute 'unsafeDiscardReferences.\"%s\"' must be a Boolean", outputName);
                            res.insert_or_assign(outputName, output.get<bool>());
                        }
                    }
                }

                return res;
            }(),
        .passAsFile =
            [&] {
                StringSet res;
                if (auto * passAsFileString = get(parsed.drv.env, "passAsFile")) {
                    if (parsed.hasStructuredAttrs()) {
                        if (shouldWarn) {
                            warn(
                                "'structuredAttrs' disables the effect of the top-level attribute 'passAsFile'; because all JSON is always passed via file");
                        }
                    } else {
                        res = tokenizeString<StringSet>(*passAsFileString);
                    }
                }
                return res;
            }(),
        .additionalSandboxProfile =
            parsed.getStringAttr("__sandboxProfile").value_or(defaults.additionalSandboxProfile),
        .noChroot = parsed.getBoolAttr("__noChroot", defaults.noChroot),
        .impureHostDeps = parsed.getStringSetAttr("__impureHostDeps").value_or(defaults.impureHostDeps),
        .impureEnvVars = parsed.getStringSetAttr("impureEnvVars").value_or(defaults.impureEnvVars),
        .allowLocalNetworking = parsed.getBoolAttr("__darwinAllowLocalNetworking", defaults.allowLocalNetworking),
        .requiredSystemFeatures =
            parsed.getStringSetAttr("requiredSystemFeatures").value_or(defaults.requiredSystemFeatures),
        .preferLocalBuild = parsed.getBoolAttr("preferLocalBuild", defaults.preferLocalBuild),
        .allowSubstitutes = parsed.getBoolAttr("allowSubstitutes", defaults.allowSubstitutes),
    };
}

StringSet DerivationOptions::getRequiredSystemFeatures(const BasicDerivation & drv) const
{
    // FIXME: cache this?
    StringSet res;
    for (auto & i : requiredSystemFeatures)
        res.insert(i);
    if (!drv.type().hasKnownOutputPaths())
        res.insert("ca-derivations");
    return res;
}

bool DerivationOptions::canBuildLocally(Store & localStore, const BasicDerivation & drv) const
{
    if (drv.platform != settings.thisSystem.get() && !settings.extraPlatforms.get().count(drv.platform)
        && !drv.isBuiltin())
        return false;

    if (settings.maxBuildJobs.get() == 0 && !drv.isBuiltin())
        return false;

    for (auto & feature : getRequiredSystemFeatures(drv))
        if (!localStore.systemFeatures.get().count(feature))
            return false;

    return true;
}

bool DerivationOptions::willBuildLocally(Store & localStore, const BasicDerivation & drv) const
{
    return preferLocalBuild && canBuildLocally(localStore, drv);
}

bool DerivationOptions::substitutesAllowed() const
{
    return settings.alwaysAllowSubstitutes ? true : allowSubstitutes;
}

bool DerivationOptions::useUidRange(const BasicDerivation & drv) const
{
    return getRequiredSystemFeatures(drv).count("uid-range");
}

}

namespace nlohmann {

using namespace nix;

DerivationOptions adl_serializer<DerivationOptions>::from_json(const json & json)
{
    return {
        .outputChecks = [&]() -> OutputChecksVariant {
            auto outputChecks = getObject(valueAt(json, "outputChecks"));

            auto forAllOutputsOpt = optionalValueAt(outputChecks, "forAllOutputs");
            auto perOutputOpt = optionalValueAt(outputChecks, "perOutput");

            if (forAllOutputsOpt && !perOutputOpt) {
                return static_cast<OutputChecks>(*forAllOutputsOpt);
            } else if (perOutputOpt && !forAllOutputsOpt) {
                return static_cast<std::map<std::string, OutputChecks>>(*perOutputOpt);
            } else {
                throw Error("Exactly one of 'perOutput' or 'forAllOutputs' is required");
            }
        }(),

        .unsafeDiscardReferences = valueAt(json, "unsafeDiscardReferences"),
        .passAsFile = getStringSet(valueAt(json, "passAsFile")),

        .additionalSandboxProfile = getString(valueAt(json, "additionalSandboxProfile")),
        .noChroot = getBoolean(valueAt(json, "noChroot")),
        .impureHostDeps = getStringSet(valueAt(json, "impureHostDeps")),
        .impureEnvVars = getStringSet(valueAt(json, "impureEnvVars")),
        .allowLocalNetworking = getBoolean(valueAt(json, "allowLocalNetworking")),

        .requiredSystemFeatures = getStringSet(valueAt(json, "requiredSystemFeatures")),
        .preferLocalBuild = getBoolean(valueAt(json, "preferLocalBuild")),
        .allowSubstitutes = getBoolean(valueAt(json, "allowSubstitutes")),
    };
}

void adl_serializer<DerivationOptions>::to_json(json & json, DerivationOptions o)
{
    json["outputChecks"] = std::visit(
        overloaded{
            [&](const OutputChecks & checks) {
                nlohmann::json outputChecks;
                outputChecks["forAllOutputs"] = checks;
                return outputChecks;
            },
            [&](const std::map<std::string, OutputChecks> & checksPerOutput) {
                nlohmann::json outputChecks;
                outputChecks["perOutput"] = checksPerOutput;
                return outputChecks;
            },
        },
        o.outputChecks);

    json["unsafeDiscardReferences"] = o.unsafeDiscardReferences;
    json["passAsFile"] = o.passAsFile;

    json["additionalSandboxProfile"] = o.additionalSandboxProfile;
    json["noChroot"] = o.noChroot;
    json["impureHostDeps"] = o.impureHostDeps;
    json["impureEnvVars"] = o.impureEnvVars;
    json["allowLocalNetworking"] = o.allowLocalNetworking;

    json["requiredSystemFeatures"] = o.requiredSystemFeatures;
    json["preferLocalBuild"] = o.preferLocalBuild;
    json["allowSubstitutes"] = o.allowSubstitutes;
}

DerivationOptions::OutputChecks adl_serializer<DerivationOptions::OutputChecks>::from_json(const json & json)
{
    return {
        .ignoreSelfRefs = getBoolean(valueAt(json, "ignoreSelfRefs")),
        .allowedReferences = nullableValueAt(json, "allowedReferences"),
        .disallowedReferences = getStringSet(valueAt(json, "disallowedReferences")),
        .allowedRequisites = nullableValueAt(json, "allowedRequisites"),
        .disallowedRequisites = getStringSet(valueAt(json, "disallowedRequisites")),
    };
}

void adl_serializer<DerivationOptions::OutputChecks>::to_json(json & json, DerivationOptions::OutputChecks c)
{
    json["ignoreSelfRefs"] = c.ignoreSelfRefs;
    json["allowedReferences"] = c.allowedReferences;
    json["disallowedReferences"] = c.disallowedReferences;
    json["allowedRequisites"] = c.allowedRequisites;
    json["disallowedRequisites"] = c.disallowedRequisites;
}

}
