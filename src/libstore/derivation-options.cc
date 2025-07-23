#include "nix/store/derivation-options.hh"
#include "nix/util/json-utils.hh"
#include "nix/store/parsed-derivations.hh"
#include "nix/store/derivations.hh"
#include "nix/store/store-api.hh"
#include "nix/util/types.hh"
#include "nix/util/util.hh"

#include <optional>
#include <string>
#include <variant>
#include <regex>

namespace nix {

static std::optional<std::string>
getStringAttr(const StringMap & env, const StructuredAttrs * parsed, const std::string & name)
{
    if (parsed) {
        auto i = parsed->structuredAttrs.find(name);
        if (i == parsed->structuredAttrs.end())
            return {};
        else {
            if (!i->is_string())
                throw Error("attribute '%s' of must be a string", name);
            return i->get<std::string>();
        }
    } else {
        auto i = env.find(name);
        if (i == env.end())
            return {};
        else
            return i->second;
    }
}

static bool getBoolAttr(const StringMap & env, const StructuredAttrs * parsed, const std::string & name, bool def)
{
    if (parsed) {
        auto i = parsed->structuredAttrs.find(name);
        if (i == parsed->structuredAttrs.end())
            return def;
        else {
            if (!i->is_boolean())
                throw Error("attribute '%s' must be a Boolean", name);
            return i->get<bool>();
        }
    } else {
        auto i = env.find(name);
        if (i == env.end())
            return def;
        else
            return i->second == "1";
    }
}

static std::optional<Strings>
getStringsAttr(const StringMap & env, const StructuredAttrs * parsed, const std::string & name)
{
    if (parsed) {
        auto i = parsed->structuredAttrs.find(name);
        if (i == parsed->structuredAttrs.end())
            return {};
        else {
            if (!i->is_array())
                throw Error("attribute '%s' must be a list of strings", name);
            Strings res;
            for (auto j = i->begin(); j != i->end(); ++j) {
                if (!j->is_string())
                    throw Error("attribute '%s' must be a list of strings", name);
                res.push_back(j->get<std::string>());
            }
            return res;
        }
    } else {
        auto i = env.find(name);
        if (i == env.end())
            return {};
        else
            return tokenizeString<Strings>(i->second);
    }
}

static std::optional<StringSet>
getStringSetAttr(const StringMap & env, const StructuredAttrs * parsed, const std::string & name)
{
    auto ss = getStringsAttr(env, parsed, name);
    return ss ? (std::optional{StringSet{ss->begin(), ss->end()}}) : (std::optional<StringSet>{});
}

using OutputChecks = DerivationOptions::OutputChecks;

using OutputChecksVariant = std::variant<OutputChecks, std::map<std::string, OutputChecks>>;

DerivationOptions
DerivationOptions::fromStructuredAttrs(const StringMap & env, const StructuredAttrs * parsed, bool shouldWarn)
{
    DerivationOptions defaults = {};

    if (shouldWarn && parsed) {
        if (get(parsed->structuredAttrs, "allowedReferences")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'allowedReferences'; use 'outputChecks' instead");
        }
        if (get(parsed->structuredAttrs, "allowedRequisites")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'allowedRequisites'; use 'outputChecks' instead");
        }
        if (get(parsed->structuredAttrs, "disallowedRequisites")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'disallowedRequisites'; use 'outputChecks' instead");
        }
        if (get(parsed->structuredAttrs, "disallowedReferences")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'disallowedReferences'; use 'outputChecks' instead");
        }
        if (get(parsed->structuredAttrs, "maxSize")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'maxSize'; use 'outputChecks' instead");
        }
        if (get(parsed->structuredAttrs, "maxClosureSize")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'maxClosureSize'; use 'outputChecks' instead");
        }
    }

    return {
        .outputChecks = [&]() -> OutputChecksVariant {
            if (parsed) {
                std::map<std::string, OutputChecks> res;
                if (auto outputChecks = get(parsed->structuredAttrs, "outputChecks")) {
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
                    .allowedReferences = getStringSetAttr(env, parsed, "allowedReferences"),
                    .disallowedReferences = getStringSetAttr(env, parsed, "disallowedReferences").value_or(StringSet{}),
                    .allowedRequisites = getStringSetAttr(env, parsed, "allowedRequisites"),
                    .disallowedRequisites = getStringSetAttr(env, parsed, "disallowedRequisites").value_or(StringSet{}),
                };
            }
        }(),
        .unsafeDiscardReferences =
            [&] {
                std::map<std::string, bool> res;

                if (parsed) {
                    if (auto udr = get(parsed->structuredAttrs, "unsafeDiscardReferences")) {
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
                if (auto * passAsFileString = get(env, "passAsFile")) {
                    if (parsed) {
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
        .exportReferencesGraph =
            [&] {
                std::map<std::string, StringSet> ret;

                if (parsed) {
                    auto e = optionalValueAt(parsed->structuredAttrs, "exportReferencesGraph");
                    if (!e || !e->is_object())
                        return ret;
                    for (auto & [key, value] : getObject(*e)) {
                        if (value.is_array())
                            ret.insert_or_assign(key, value);
                        else if (value.is_string())
                            ret.insert_or_assign(key, StringSet{value});
                        else
                            throw Error("'exportReferencesGraph' value is not an array or a string");
                    }
                } else {
                    auto s = getOr(env, "exportReferencesGraph", "");
                    Strings ss = tokenizeString<Strings>(s);
                    if (ss.size() % 2 != 0)
                        throw Error("odd number of tokens in 'exportReferencesGraph': '%1%'", s);
                    for (Strings::iterator i = ss.begin(); i != ss.end();) {
                        auto fileName = std::move(*i++);
                        static std::regex regex("[A-Za-z_][A-Za-z0-9_.-]*");
                        if (!std::regex_match(fileName, regex))
                            throw Error("invalid file name '%s' in 'exportReferencesGraph'", fileName);

                        auto & storePathS = *i++;
                        ret.insert_or_assign(std::move(fileName), StringSet{storePathS});
                    }
                }
                return ret;
            }(),
        .additionalSandboxProfile =
            getStringAttr(env, parsed, "__sandboxProfile").value_or(defaults.additionalSandboxProfile),
        .noChroot = getBoolAttr(env, parsed, "__noChroot", defaults.noChroot),
        .impureHostDeps = getStringSetAttr(env, parsed, "__impureHostDeps").value_or(defaults.impureHostDeps),
        .impureEnvVars = getStringSetAttr(env, parsed, "impureEnvVars").value_or(defaults.impureEnvVars),
        .allowLocalNetworking = getBoolAttr(env, parsed, "__darwinAllowLocalNetworking", defaults.allowLocalNetworking),
        .requiredSystemFeatures =
            getStringSetAttr(env, parsed, "requiredSystemFeatures").value_or(defaults.requiredSystemFeatures),
        .preferLocalBuild = getBoolAttr(env, parsed, "preferLocalBuild", defaults.preferLocalBuild),
        .allowSubstitutes = getBoolAttr(env, parsed, "allowSubstitutes", defaults.allowSubstitutes),
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
        if (!localStore.config.systemFeatures.get().count(feature))
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

} // namespace nix

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

} // namespace nlohmann
