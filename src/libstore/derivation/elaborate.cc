#include "nix/store/derivation/elaborate.hh"
#include "nix/util/json-utils.hh"
#include "nix/store/parsed-derivations.hh"
#include "nix/store/derivations.hh"
#include "nix/store/derived-path.hh"
#include "nix/store/store-api.hh"
#include "nix/util/types.hh"
#include "nix/util/util.hh"

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
using OutputChecksVariant = std::
    variant<derivation::OutputChecks<Inputs>, std::map<std::string, derivation::OutputChecks<Inputs>, std::less<>>>;

DerivationOptions<StorePath> derivationOptionsFromStructuredAttrs(
    const StoreDirConfig & store,
    const StringMap & env,
    const StructuredAttrs * parsed,
    bool shouldWarn,
    const ExperimentalFeatureSettings & mockXpSettings)
{
    /* Use the SingleDerivedPath version with empty inputs, then
       resolve. */
    std::set<SingleDerivedPath> emptyInputs{};
    auto singleDerivedPathOptions =
        derivationOptionsFromStructuredAttrs(store, emptyInputs, env, parsed, shouldWarn, mockXpSettings);

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
    const std::set<SingleDerivedPath> & inputs,
    const StringMap & env,
    const StructuredAttrs * parsed,
    bool shouldWarn,
    const ExperimentalFeatureSettings & mockXpSettings)
{
    using namespace derivation;

    DerivationOptions<SingleDerivedPath> defaults = {};

    std::map<std::string, SingleDerivedPath::Built, std::less<>> placeholders;
    if (mockXpSettings.isEnabled(Xp::CaDerivations)) {
        /* Initialize placeholder map from inputs */
        for (const auto & input : inputs) {
            if (auto * built = std::get_if<SingleDerivedPath::Built>(&input.raw())) {
                placeholders.insert_or_assign(
                    DownstreamPlaceholder::fromSingleDerivedPathBuilt(*built, mockXpSettings).render(), *built);
            }
        }
    }

    /* Extract the placeholder key from a path that may have a subpath
       appended (e.g. `/HASH/foo` → `/HASH`), mirroring how
       `StoreDirConfig::toStorePath` strips subpaths from store paths. */
    auto findPlaceholder = [&](std::string_view pathS) -> const SingleDerivedPath::Built * {
        auto slash = pathS.find('/', 1);
        auto key = pathS.substr(0, slash);
        if (auto it = placeholders.find(key); it != placeholders.end())
            return &it->second;
        return nullptr;
    };

    auto parseSingleDerivedPath = [&](const std::string & pathS) -> SingleDerivedPath {
        if (auto * built = findPlaceholder(pathS))
            return *built;
        else
            return SingleDerivedPath::Opaque{store.toStorePath(pathS).first};
    };

    auto parseRef = [&](const std::string & pathS) -> DrvRef<SingleDerivedPath> {
        if (auto * built = findPlaceholder(pathS))
            return *built;
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

                std::map<std::string, OutputChecks<SingleDerivedPath>, std::less<>> res;
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
                std::map<std::string, bool, std::less<>> res;

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
                std::map<std::string, std::set<SingleDerivedPath>, std::less<>> ret;

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
    auto tryResolveOutputChecks = [&](const derivation::OutputChecks<SingleDerivedPath> & checks)
        -> std::optional<derivation::OutputChecks<StorePath>> {
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

        return derivation::OutputChecks<StorePath>{
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
    auto tryResolveExportReferencesGraph =
        [&](const std::map<std::string, std::set<SingleDerivedPath>, std::less<>> & exportGraph)
        -> std::optional<std::map<std::string, std::set<StorePath>, std::less<>>> {
        std::map<std::string, std::set<StorePath>, std::less<>> resolved;
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
            [&](const derivation::OutputChecks<SingleDerivedPath> & checks)
                -> std::optional<std::variant<
                    derivation::OutputChecks<StorePath>,
                    std::map<std::string, derivation::OutputChecks<StorePath>, std::less<>>>> {
                auto resolved = tryResolveOutputChecks(checks);
                if (!resolved)
                    return std::nullopt;
                return std::variant<
                    derivation::OutputChecks<StorePath>,
                    std::map<std::string, derivation::OutputChecks<StorePath>, std::less<>>>(*resolved);
            },
            [&](const std::map<std::string, derivation::OutputChecks<SingleDerivedPath>, std::less<>> & checksMap)
                -> std::optional<std::variant<
                    derivation::OutputChecks<StorePath>,
                    std::map<std::string, derivation::OutputChecks<StorePath>, std::less<>>>> {
                std::map<std::string, derivation::OutputChecks<StorePath>, std::less<>> resolvedMap;
                for (const auto & [outputName, checks] : checksMap) {
                    auto resolved = tryResolveOutputChecks(checks);
                    if (!resolved)
                        return std::nullopt;
                    resolvedMap.emplace(outputName, *resolved);
                }
                return std::variant<
                    derivation::OutputChecks<StorePath>,
                    std::map<std::string, derivation::OutputChecks<StorePath>, std::less<>>>(resolvedMap);
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

} // namespace nix
