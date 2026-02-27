#include "nix/store/derivation-options.hh"
#include "nix/util/json-utils.hh"
#include "nix/store/parsed-derivations.hh"
#include "nix/store/derivations.hh"
#include "nix/store/derived-path.hh"
#include "nix/store/store-api.hh"
#include "nix/util/types.hh"
#include "nix/util/util.hh"
#include "nix/util/variant-wrapper.hh"

#include <optional>
#include <string>
#include <variant>
#include <regex>
#include <ranges>

namespace nix {

static std::optional<std::string>
getStringAttr(const StringMap & env, const StructuredAttrs * parsed, const std::string & name)
{
    if (parsed) {
        if (auto * i = get(parsed->structuredAttrs, name))
            try {
                return getString(*i);
            } catch (Error & e) {
                e.addTrace({}, "while parsing attribute \"%s\"", name);
                throw;
            }
    } else {
        if (auto * i = get(env, name))
            return *i;
    }
    return {};
}

static bool getBoolAttr(const StringMap & env, const StructuredAttrs * parsed, const std::string & name, bool def)
{
    if (parsed) {
        if (auto * i = get(parsed->structuredAttrs, name))
            try {
                return getBoolean(*i);
            } catch (Error & e) {
                e.addTrace({}, "while parsing attribute \"%s\"", name);
                throw;
            }
    } else {
        if (auto * i = get(env, name))
            return *i == "1";
    }
    return def;
}

static std::optional<StringSet>
getStringSetAttr(const StringMap & env, const StructuredAttrs * parsed, const std::string & name)
{
    if (parsed) {
        if (auto * i = get(parsed->structuredAttrs, name))
            try {
                return getStringSet(*i);
            } catch (Error & e) {
                e.addTrace({}, "while parsing attribute \"%s\"", name);
                throw;
            }
    } else {
        if (auto * i = get(env, name))
            return tokenizeString<StringSet>(*i);
    }
    return {};
}

template<typename Inputs>
using OutputChecks = DerivationOptions<Inputs>::OutputChecks;

template<typename Inputs>
using OutputChecksVariant = std::variant<OutputChecks<Inputs>, std::map<std::string, OutputChecks<Inputs>>>;

DerivationOptions<StorePath> derivationOptionsFromStructuredAttrs(
    const StoreDirConfig & store,
    const StringMap & env,
    const StructuredAttrs * parsed,
    bool shouldWarn,
    const ExperimentalFeatureSettings & mockXpSettings)
{
    /* Use the SingleDerivedPath version with empty inputDrvs, then
       resolve. */
    DerivedPathMap<StringSet> emptyInputDrvs{};
    auto singleDerivedPathOptions =
        derivationOptionsFromStructuredAttrs(store, emptyInputDrvs, env, parsed, shouldWarn, mockXpSettings);

    /* "Resolve" all SingleDerivedPath inputs to StorePath. */
    auto resolved = tryResolve(
        singleDerivedPathOptions,
        [&](ref<const SingleDerivedPath> drvPath, const std::string & outputName) -> std::optional<StorePath> {
            // there should be nothing to resolve
            assert(false);
        });

    /* Since we should never need to call the call back, there should be
       no way it fails. */
    assert(resolved);

    return *resolved;
}

static void flatten(const nlohmann::json & value, StringSet & res)
{
    if (value.is_array())
        for (auto & v : value)
            flatten(v, res);
    else if (value.is_string())
        res.insert(value);
    else
        throw Error("'exportReferencesGraph' value is not an array or a string");
}

DerivationOptions<SingleDerivedPath> derivationOptionsFromStructuredAttrs(
    const StoreDirConfig & store,
    const DerivedPathMap<StringSet> & inputDrvs,
    const StringMap & env,
    const StructuredAttrs * parsed,
    bool shouldWarn,
    const ExperimentalFeatureSettings & mockXpSettings)
{
    DerivationOptions<SingleDerivedPath> defaults = {};

    std::map<std::string, SingleDerivedPath::Built> placeholders;
    if (mockXpSettings.isEnabled(Xp::CaDerivations)) {
        /* Initialize placeholder map from inputDrvs */
        auto initPlaceholders = [&](this const auto & initPlaceholders,
                                    ref<const SingleDerivedPath> basePath,
                                    const DerivedPathMap<StringSet>::ChildNode & node) -> void {
            for (const auto & outputName : node.value) {
                auto built = SingleDerivedPath::Built{
                    .drvPath = basePath,
                    .output = outputName,
                };
                placeholders.insert_or_assign(
                    DownstreamPlaceholder::fromSingleDerivedPathBuilt(built, mockXpSettings).render(),
                    std::move(built));
            }

            for (const auto & [outputName, childNode] : node.childMap) {
                initPlaceholders(
                    make_ref<const SingleDerivedPath>(SingleDerivedPath::Built{
                        .drvPath = basePath,
                        .output = outputName,
                    }),
                    childNode);
            }
        };

        for (const auto & [drvPath, outputs] : inputDrvs.map) {
            auto basePath = make_ref<const SingleDerivedPath>(SingleDerivedPath::Opaque{drvPath});
            initPlaceholders(basePath, outputs);
        }
    }

    auto parseSingleDerivedPath = [&](const std::string & pathS) -> SingleDerivedPath {
        if (auto it = placeholders.find(pathS); it != placeholders.end())
            return it->second;
        else
            return SingleDerivedPath::Opaque{store.toStorePath(pathS).first};
    };

    auto parseRef = [&](const std::string & pathS) -> DrvRef<SingleDerivedPath> {
        if (auto it = placeholders.find(pathS); it != placeholders.end())
            return it->second;
        if (store.isStorePath(pathS))
            return SingleDerivedPath::Opaque{store.toStorePath(pathS).first};
        else
            return pathS;
    };

    if (shouldWarn && parsed) {
        auto & structuredAttrs = parsed->structuredAttrs;

        if (get(structuredAttrs, "allowedReferences")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'allowedReferences'; use 'outputChecks' instead");
        }
        if (get(structuredAttrs, "allowedRequisites")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'allowedRequisites'; use 'outputChecks' instead");
        }
        if (get(structuredAttrs, "disallowedRequisites")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'disallowedRequisites'; use 'outputChecks' instead");
        }
        if (get(structuredAttrs, "disallowedReferences")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'disallowedReferences'; use 'outputChecks' instead");
        }
        if (get(structuredAttrs, "maxSize")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'maxSize'; use 'outputChecks' instead");
        }
        if (get(structuredAttrs, "maxClosureSize")) {
            warn(
                "'structuredAttrs' disables the effect of the top-level attribute 'maxClosureSize'; use 'outputChecks' instead");
        }
    }

    return {
        .outputChecks = [&]() -> OutputChecksVariant<SingleDerivedPath> {
            if (parsed) {
                auto & structuredAttrs = parsed->structuredAttrs;

                std::map<std::string, OutputChecks<SingleDerivedPath>> res;
                if (auto * outputChecks = get(structuredAttrs, "outputChecks")) {
                    for (auto & [outputName, output_] : getObject(*outputChecks)) {
                        auto & output = getObject(output_);

                        auto get_ =
                            [&](const std::string & name) -> std::optional<std::set<DrvRef<SingleDerivedPath>>> {
                            if (auto * i = get(output, name)) {
                                try {
                                    std::set<DrvRef<SingleDerivedPath>> res;
                                    for (auto & s : getStringList(*i))
                                        res.insert(parseRef(s));
                                    return res;
                                } catch (Error & e) {
                                    e.addTrace(
                                        {}, "while parsing attribute 'outputChecks.\"%s\".%s'", outputName, name);
                                    throw;
                                }
                            }
                            return {};
                        };

                        res.insert_or_assign(
                            outputName,
                            OutputChecks<SingleDerivedPath>{
                                .maxSize = ptrToOwned<uint64_t>(get(output, "maxSize")),
                                .maxClosureSize = ptrToOwned<uint64_t>(get(output, "maxClosureSize")),
                                .allowedReferences = get_("allowedReferences"),
                                .disallowedReferences =
                                    get_("disallowedReferences").value_or(std::set<DrvRef<SingleDerivedPath>>{}),
                                .allowedRequisites = get_("allowedRequisites"),
                                .disallowedRequisites =
                                    get_("disallowedRequisites").value_or(std::set<DrvRef<SingleDerivedPath>>{}),
                            });
                    }
                }
                return res;
            } else {
                auto parseRefSet = [&](const std::optional<StringSet> optionalStringSet)
                    -> std::optional<std::set<DrvRef<SingleDerivedPath>>> {
                    if (!optionalStringSet)
                        return std::nullopt;
                    auto range = *optionalStringSet | std::views::transform(parseRef);
                    return std::set<DrvRef<SingleDerivedPath>>(range.begin(), range.end());
                };
                return OutputChecks<SingleDerivedPath>{
                    // legacy non-structured-attributes case
                    .ignoreSelfRefs = true,
                    .allowedReferences = parseRefSet(getStringSetAttr(env, parsed, "allowedReferences")),
                    .disallowedReferences = parseRefSet(getStringSetAttr(env, parsed, "disallowedReferences"))
                                                .value_or(std::set<DrvRef<SingleDerivedPath>>{}),
                    .allowedRequisites = parseRefSet(getStringSetAttr(env, parsed, "allowedRequisites")),
                    .disallowedRequisites = parseRefSet(getStringSetAttr(env, parsed, "disallowedRequisites"))
                                                .value_or(std::set<DrvRef<SingleDerivedPath>>{}),
                };
            }
        }(),
        .unsafeDiscardReferences =
            [&] {
                std::map<std::string, bool> res;

                if (parsed) {
                    if (auto * udr = get(parsed->structuredAttrs, "unsafeDiscardReferences")) {
                        try {
                            for (auto & [outputName, output] : getObject(*udr))
                                res.insert_or_assign(outputName, getBoolean(output));
                        } catch (Error & e) {
                            e.addTrace({}, "while parsing attribute 'unsafeDiscardReferences'");
                            throw;
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
                std::map<std::string, std::set<SingleDerivedPath>> ret;

                if (parsed) {
                    auto * e = get(parsed->structuredAttrs, "exportReferencesGraph");
                    if (!e)
                        return ret;
                    if (!e->is_object()) {
                        warn("'exportReferencesGraph' in structured attrs is not a JSON object, ignoring");
                        return ret;
                    }
                    for (auto & [key, storePathsJson] : getObject(*e)) {
                        StringSet ss;
                        flatten(storePathsJson, ss);
                        std::set<SingleDerivedPath> storePaths;
                        for (auto & s : ss)
                            storePaths.insert(parseSingleDerivedPath(s));
                        ret.insert_or_assign(key, std::move(storePaths));
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
                        ret.insert_or_assign(std::move(fileName), std::set{parseSingleDerivedPath(storePathS)});
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

template<typename Input>
StringSet DerivationOptions<Input>::getRequiredSystemFeatures(const BasicDerivation & drv) const
{
    // FIXME: cache this?
    StringSet res;
    for (auto & i : requiredSystemFeatures)
        res.insert(i);
    if (!drv.type().hasKnownOutputPaths())
        res.insert("ca-derivations");
    return res;
}

template<typename Input>
bool DerivationOptions<Input>::substitutesAllowed(const WorkerSettings & workerSettings) const
{
    return workerSettings.alwaysAllowSubstitutes ? true : allowSubstitutes;
}

template<typename Input>
bool DerivationOptions<Input>::useUidRange(const BasicDerivation & drv) const
{
    return getRequiredSystemFeatures(drv).count("uid-range");
}

std::optional<DerivationOptions<StorePath>> tryResolve(
    const DerivationOptions<SingleDerivedPath> & drvOptions,
    fun<std::optional<StorePath>(ref<const SingleDerivedPath> drvPath, const std::string & outputName)>
        queryResolutionChain)
{
    auto tryResolvePath = [&](const SingleDerivedPath & input) -> std::optional<StorePath> {
        return std::visit(
            overloaded{
                [](const SingleDerivedPath::Opaque & p) -> std::optional<StorePath> { return p.path; },
                [&](const SingleDerivedPath::Built & p) -> std::optional<StorePath> {
                    return queryResolutionChain(p.drvPath, p.output);
                }},
            input.raw());
    };

    auto tryResolveRef = [&](const DrvRef<SingleDerivedPath> & ref) -> std::optional<DrvRef<StorePath>> {
        return std::visit(
            overloaded{
                [](const OutputName & outputName) -> std::optional<DrvRef<StorePath>> { return outputName; },
                [&](const SingleDerivedPath & input) -> std::optional<DrvRef<StorePath>> {
                    return tryResolvePath(input);
                }},
            ref);
    };

    auto tryResolveRefSet =
        [&](const std::set<DrvRef<SingleDerivedPath>> & refSet) -> std::optional<std::set<DrvRef<StorePath>>> {
        std::set<DrvRef<StorePath>> resolvedSet;
        for (const auto & ref : refSet) {
            auto resolvedRef = tryResolveRef(ref);
            if (!resolvedRef)
                return std::nullopt;
            resolvedSet.insert(*resolvedRef);
        }
        return resolvedSet;
    };

    // Helper function to try resolving OutputChecks using functional style
    auto tryResolveOutputChecks = [&](const DerivationOptions<SingleDerivedPath>::OutputChecks & checks)
        -> std::optional<DerivationOptions<StorePath>::OutputChecks> {
        std::optional<std::set<DrvRef<StorePath>>> resolvedAllowedReferences;
        if (checks.allowedReferences) {
            resolvedAllowedReferences = tryResolveRefSet(*checks.allowedReferences);
            if (!resolvedAllowedReferences)
                return std::nullopt;
        }

        std::optional<std::set<DrvRef<StorePath>>> resolvedAllowedRequisites;
        if (checks.allowedRequisites) {
            resolvedAllowedRequisites = tryResolveRefSet(*checks.allowedRequisites);
            if (!resolvedAllowedRequisites)
                return std::nullopt;
        }

        auto resolvedDisallowedReferences = tryResolveRefSet(checks.disallowedReferences);
        if (!resolvedDisallowedReferences)
            return std::nullopt;

        auto resolvedDisallowedRequisites = tryResolveRefSet(checks.disallowedRequisites);
        if (!resolvedDisallowedRequisites)
            return std::nullopt;

        return DerivationOptions<StorePath>::OutputChecks{
            .ignoreSelfRefs = checks.ignoreSelfRefs,
            .maxSize = checks.maxSize,
            .maxClosureSize = checks.maxClosureSize,
            .allowedReferences = resolvedAllowedReferences,
            .disallowedReferences = *resolvedDisallowedReferences,
            .allowedRequisites = resolvedAllowedRequisites,
            .disallowedRequisites = *resolvedDisallowedRequisites,
        };
    };

    // Helper function to resolve exportReferencesGraph using functional style
    auto tryResolveExportReferencesGraph = [&](const std::map<std::string, std::set<SingleDerivedPath>> & exportGraph)
        -> std::optional<std::map<std::string, std::set<StorePath>>> {
        std::map<std::string, std::set<StorePath>> resolved;
        for (const auto & [name, inputPaths] : exportGraph) {
            std::set<StorePath> resolvedPaths;
            for (const auto & inputPath : inputPaths) {
                auto resolvedPath = tryResolvePath(inputPath);
                if (!resolvedPath)
                    return std::nullopt;
                resolvedPaths.insert(*resolvedPath);
            }
            resolved.emplace(name, std::move(resolvedPaths));
        }
        return resolved;
    };

    // Resolve outputChecks using functional style with std::visit
    auto resolvedOutputChecks = std::visit(
        overloaded{
            [&](const DerivationOptions<SingleDerivedPath>::OutputChecks & checks)
                -> std::optional<std::variant<
                    DerivationOptions<StorePath>::OutputChecks,
                    std::map<std::string, DerivationOptions<StorePath>::OutputChecks>>> {
                auto resolved = tryResolveOutputChecks(checks);
                if (!resolved)
                    return std::nullopt;
                return std::variant<
                    DerivationOptions<StorePath>::OutputChecks,
                    std::map<std::string, DerivationOptions<StorePath>::OutputChecks>>(*resolved);
            },
            [&](const std::map<std::string, DerivationOptions<SingleDerivedPath>::OutputChecks> & checksMap)
                -> std::optional<std::variant<
                    DerivationOptions<StorePath>::OutputChecks,
                    std::map<std::string, DerivationOptions<StorePath>::OutputChecks>>> {
                std::map<std::string, DerivationOptions<StorePath>::OutputChecks> resolvedMap;
                for (const auto & [outputName, checks] : checksMap) {
                    auto resolved = tryResolveOutputChecks(checks);
                    if (!resolved)
                        return std::nullopt;
                    resolvedMap.emplace(outputName, *resolved);
                }
                return std::variant<
                    DerivationOptions<StorePath>::OutputChecks,
                    std::map<std::string, DerivationOptions<StorePath>::OutputChecks>>(resolvedMap);
            }},
        drvOptions.outputChecks);

    if (!resolvedOutputChecks)
        return std::nullopt;

    // Resolve exportReferencesGraph
    auto resolvedExportGraph = tryResolveExportReferencesGraph(drvOptions.exportReferencesGraph);
    if (!resolvedExportGraph)
        return std::nullopt;

    // Return resolved DerivationOptions using designated initializers
    return DerivationOptions<StorePath>{
        .outputChecks = *resolvedOutputChecks,
        .unsafeDiscardReferences = drvOptions.unsafeDiscardReferences,
        .passAsFile = drvOptions.passAsFile,
        .exportReferencesGraph = *resolvedExportGraph,
        .additionalSandboxProfile = drvOptions.additionalSandboxProfile,
        .noChroot = drvOptions.noChroot,
        .impureHostDeps = drvOptions.impureHostDeps,
        .impureEnvVars = drvOptions.impureEnvVars,
        .allowLocalNetworking = drvOptions.allowLocalNetworking,
        .requiredSystemFeatures = drvOptions.requiredSystemFeatures,
        .preferLocalBuild = drvOptions.preferLocalBuild,
        .allowSubstitutes = drvOptions.allowSubstitutes,
    };
}

template struct DerivationOptions<StorePath>;
template struct DerivationOptions<SingleDerivedPath>;

} // namespace nix

namespace nlohmann {

using namespace nix;

DerivationOptions<SingleDerivedPath> adl_serializer<DerivationOptions<SingleDerivedPath>>::from_json(const json & json_)
{
    auto & json = getObject(json_);

    return {
        .outputChecks = [&]() -> OutputChecksVariant<SingleDerivedPath> {
            auto outputChecks = getObject(valueAt(json, "outputChecks"));

            auto forAllOutputsOpt = get(outputChecks, "forAllOutputs");
            auto perOutputOpt = get(outputChecks, "perOutput");

            if (forAllOutputsOpt && !perOutputOpt) {
                return static_cast<OutputChecks<SingleDerivedPath>>(*forAllOutputsOpt);
            } else if (perOutputOpt && !forAllOutputsOpt) {
                return static_cast<std::map<std::string, OutputChecks<SingleDerivedPath>>>(*perOutputOpt);
            } else {
                throw Error("Exactly one of 'perOutput' or 'forAllOutputs' is required");
            }
        }(),

        .unsafeDiscardReferences = valueAt(json, "unsafeDiscardReferences"),
        .passAsFile = getStringSet(valueAt(json, "passAsFile")),
        .exportReferencesGraph = valueAt(json, "exportReferencesGraph"),

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

void adl_serializer<DerivationOptions<SingleDerivedPath>>::to_json(
    json & json, const DerivationOptions<SingleDerivedPath> & o)
{
    json["outputChecks"] = std::visit(
        overloaded{
            [&](const OutputChecks<SingleDerivedPath> & checks) {
                nlohmann::json outputChecks;
                outputChecks["forAllOutputs"] = checks;
                return outputChecks;
            },
            [&](const std::map<std::string, OutputChecks<SingleDerivedPath>> & checksPerOutput) {
                nlohmann::json outputChecks;
                outputChecks["perOutput"] = checksPerOutput;
                return outputChecks;
            },
        },
        o.outputChecks);

    json["unsafeDiscardReferences"] = o.unsafeDiscardReferences;
    json["passAsFile"] = o.passAsFile;
    json["exportReferencesGraph"] = o.exportReferencesGraph;

    json["additionalSandboxProfile"] = o.additionalSandboxProfile;
    json["noChroot"] = o.noChroot;
    json["impureHostDeps"] = o.impureHostDeps;
    json["impureEnvVars"] = o.impureEnvVars;
    json["allowLocalNetworking"] = o.allowLocalNetworking;

    json["requiredSystemFeatures"] = o.requiredSystemFeatures;
    json["preferLocalBuild"] = o.preferLocalBuild;
    json["allowSubstitutes"] = o.allowSubstitutes;
}

OutputChecks<SingleDerivedPath> adl_serializer<OutputChecks<SingleDerivedPath>>::from_json(const json & json_)
{
    auto & json = getObject(json_);

    return {
        .ignoreSelfRefs = getBoolean(valueAt(json, "ignoreSelfRefs")),
        .maxSize = ptrToOwned<uint64_t>(getNullable(valueAt(json, "maxSize"))),
        .maxClosureSize = ptrToOwned<uint64_t>(getNullable(valueAt(json, "maxClosureSize"))),
        .allowedReferences =
            ptrToOwned<std::set<DrvRef<SingleDerivedPath>>>(getNullable(valueAt(json, "allowedReferences"))),
        .disallowedReferences = valueAt(json, "disallowedReferences"),
        .allowedRequisites =
            ptrToOwned<std::set<DrvRef<SingleDerivedPath>>>(getNullable(valueAt(json, "allowedRequisites"))),
        .disallowedRequisites = valueAt(json, "disallowedRequisites"),
    };
}

void adl_serializer<OutputChecks<SingleDerivedPath>>::to_json(json & json, const OutputChecks<SingleDerivedPath> & c)
{
    json["ignoreSelfRefs"] = c.ignoreSelfRefs;
    json["maxSize"] = c.maxSize;
    json["maxClosureSize"] = c.maxClosureSize;
    json["allowedReferences"] = c.allowedReferences;
    json["disallowedReferences"] = c.disallowedReferences;
    json["allowedRequisites"] = c.allowedRequisites;
    json["disallowedRequisites"] = c.disallowedRequisites;
}

} // namespace nlohmann
