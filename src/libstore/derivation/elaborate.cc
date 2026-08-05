#include "nix/store/derivation/elaborate.hh"
#include "nix/store/derivation/aterm.hh"
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
using OutputChecks = derivation::OutputChecks<Inputs>;

template<typename Inputs>
using OutputChecksVariant =
    std::variant<OutputChecks<Inputs>, std::map<std::string, OutputChecks<Inputs>, std::less<>>>;

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

template<typename Input>
void elaborateLegacyOptions(
    const StoreDirConfig & store,
    derivation::Derivation<Input> & drv,
    bool shouldWarn,
    const ExperimentalFeatureSettings & mockXpSettings)
{
    const auto & inputs = drv.inputs;
    StringMap env;
    for (auto & [name, var] : drv.env)
        env.insert_or_assign(name, var.value);
    const StructuredAttrs * parsed = drv.structuredAttrs ? &*drv.structuredAttrs : nullptr;

    const derivation::TopOptions<Input> defaults = {};

    std::map<std::string, SingleDerivedPath::Built, std::less<>> placeholders;
    if constexpr (std::is_same_v<Input, SingleDerivedPath>) {
        if (mockXpSettings.isEnabled(Xp::CaDerivations)) {
            /* Initialize placeholder map from inputs */
            for (const auto & input : inputs) {
                if (auto * built = std::get_if<SingleDerivedPath::Built>(&input.raw())) {
                    placeholders.insert_or_assign(
                        DownstreamPlaceholder::fromSingleDerivedPathBuilt(*built, mockXpSettings).render(), *built);
                }
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

    auto parseInput = [&](const std::string & pathS) -> Input {
        if constexpr (std::is_same_v<Input, SingleDerivedPath>) {
            if (auto * built = findPlaceholder(pathS))
                return *built;
            return SingleDerivedPath::Opaque{store.toStorePath(pathS).first};
        } else {
            return store.toStorePath(pathS).first;
        }
    };

    auto parseRef = [&](const std::string & pathS) -> DrvRef<Input> {
        if constexpr (std::is_same_v<Input, SingleDerivedPath>) {
            if (auto * built = findPlaceholder(pathS))
                return DrvRef<Input>{SingleDerivedPath{SingleDerivedPath::Built{*built}}};
        }
        if (store.isStorePath(pathS))
            return DrvRef<Input>{parseInput(pathS)};
        else
            return DrvRef<Input>{pathS};
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

    if (parsed) {
        auto & structuredAttrs = parsed->structuredAttrs;

        if (auto * outputChecks = get(structuredAttrs, "outputChecks")) {
            for (auto & [outputName, output_] : getObject(*outputChecks)) {
                auto & output = getObject(output_);

                auto get_ = [&](const std::string & name) -> std::optional<std::set<DrvRef<Input>>> {
                    if (auto * i = get(output, name)) {
                        try {
                            std::set<DrvRef<Input>> res;
                            for (auto & s : getStringList(*i))
                                res.insert(parseRef(s));
                            return res;
                        } catch (Error & e) {
                            e.addTrace({}, "while parsing attribute 'outputChecks.\"%s\".%s'", outputName, name);
                            throw;
                        }
                    }
                    return {};
                };

                auto checks = derivation::OutputChecks<Input>{
                    .maxSize = ptrToOwned<uint64_t>(get(output, "maxSize")),
                    .maxClosureSize = ptrToOwned<uint64_t>(get(output, "maxClosureSize")),
                    .allowedReferences = get_("allowedReferences"),
                    .disallowedReferences = get_("disallowedReferences").value_or(std::set<DrvRef<Input>>{}),
                    .allowedRequisites = get_("allowedRequisites"),
                    .disallowedRequisites = get_("disallowedRequisites").value_or(std::set<DrvRef<Input>>{}),
                };

                if (auto it = drv.outputs.find(outputName); it != drv.outputs.end())
                    it->second.options.checks = std::move(checks);
            }
        }
    } else {
        auto parseRefSet =
            [&](const std::optional<StringSet> optionalStringSet) -> std::optional<std::set<DrvRef<Input>>> {
            if (!optionalStringSet)
                return std::nullopt;
            auto range = *optionalStringSet | std::views::transform(parseRef);
            return std::set<DrvRef<Input>>(range.begin(), range.end());
        };
        auto checks = derivation::OutputChecks<Input>{
            // legacy non-structured-attributes case
            .ignoreSelfRefs = true,
            .allowedReferences = parseRefSet(getStringSetAttr(env, parsed, "allowedReferences")),
            .disallowedReferences =
                parseRefSet(getStringSetAttr(env, parsed, "disallowedReferences")).value_or(std::set<DrvRef<Input>>{}),
            .allowedRequisites = parseRefSet(getStringSetAttr(env, parsed, "allowedRequisites")),
            .disallowedRequisites =
                parseRefSet(getStringSetAttr(env, parsed, "disallowedRequisites")).value_or(std::set<DrvRef<Input>>{}),
        };
        /* Trivial checks (which check nothing) are canonically
           represented as no checks at all, so that the ATerm round-trip
           is stable. */
        if (checks != derivation::OutputChecks<Input>{.ignoreSelfRefs = true})
            drv.options.allOutputChecks = std::move(checks);
    }
    if (parsed) {
        if (auto * udr = get(parsed->structuredAttrs, "unsafeDiscardReferences")) {
            try {
                for (auto & [outputName, output] : getObject(*udr))
                    if (auto it = drv.outputs.find(outputName); it != drv.outputs.end())
                        it->second.options.unsafeDiscardReferences = getBoolean(output);
            } catch (Error & e) {
                e.addTrace({}, "while parsing attribute 'unsafeDiscardReferences'");
                throw;
            }
        }
    }
    if (auto * passAsFileString = get(env, "passAsFile")) {
        if (parsed) {
            if (shouldWarn) {
                warn(
                    "'structuredAttrs' disables the effect of the top-level attribute 'passAsFile'; because all JSON is always passed via file");
            }
        } else {
            for (auto & name : tokenizeString<StringSet>(*passAsFileString))
                if (auto it = drv.env.find(name); it != drv.env.end())
                    it->second.passAsFile = true;
        }
    }
    drv.options.exportReferencesGraph = [&] {
        std::map<std::string, std::set<Input>, std::less<>> ret;

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
                std::set<Input> storePaths;
                for (auto & s : ss)
                    storePaths.insert(parseInput(s));
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
                ret.insert_or_assign(std::move(fileName), std::set{parseInput(storePathS)});
            }
        }
        return ret;
    }();
    drv.options.additionalSandboxProfile =
        getStringAttr(env, parsed, "__sandboxProfile").value_or(defaults.additionalSandboxProfile);
    drv.options.noChroot = getBoolAttr(env, parsed, "__noChroot", defaults.noChroot);
    drv.options.impureHostDeps = getStringSetAttr(env, parsed, "__impureHostDeps").value_or(defaults.impureHostDeps);
    drv.options.impureEnvVars = getStringSetAttr(env, parsed, "impureEnvVars").value_or(defaults.impureEnvVars);
    drv.options.allowLocalNetworking =
        getBoolAttr(env, parsed, "__darwinAllowLocalNetworking", defaults.allowLocalNetworking);
    drv.options.requiredSystemFeatures =
        getStringSetAttr(env, parsed, "requiredSystemFeatures").value_or(defaults.requiredSystemFeatures);
    drv.options.preferLocalBuild = getBoolAttr(env, parsed, "preferLocalBuild", defaults.preferLocalBuild);
    drv.options.allowSubstitutes = getBoolAttr(env, parsed, "allowSubstitutes", defaults.allowSubstitutes);
}

/**
 * The shared part of the two `elaborate` overloads: everything but the
 * inputs, which are the one field whose shape differs.
 */
template<typename Input, typename Inputs>
static derivation::Derivation<Input, derivation::Output>
elaborateCommon(const derivation::ATermT<Inputs> & aterm, std::string_view name)
{
    derivation::Derivation<Input, derivation::Output> drv{
        .name = std::string{name},
        .platform = aterm.platform,
        .builder = aterm.builder,
        .args = aterm.args,
    };

    for (auto & [outputName, output] : aterm.outputs)
        drv.outputs.insert_or_assign(
            outputName,
            derivation::OutputWithOptions<Input, derivation::Output>{
                .output = output,
            });

    /* Split out the structured attributes from the environment. */
    for (auto & [name, value] : aterm.env) {
        if (name == StructuredAttrs::envVarName)
            drv.structuredAttrs = StructuredAttrs::parse(value);
        else
            drv.env.insert_or_assign(name, derivation::EnvValue{.value = value});
    }

    return drv;
}

namespace derivation {

Full elaborate(
    const ATerm & aterm,
    const StoreDirConfig & store,
    std::string_view name,
    const ExperimentalFeatureSettings & xpSettings)
{
    auto drv = elaborateCommon<SingleDerivedPath>(aterm, name);
    drv.inputs = aterm.inputs.toSet();
    elaborateLegacyOptions(store, drv, true, xpSettings);
    return drv;
}

Basic elaborate(
    const BasicATerm & aterm,
    const StoreDirConfig & store,
    std::string_view name,
    const ExperimentalFeatureSettings & xpSettings)
{
    auto drv = elaborateCommon<StorePath>(aterm, name);
    drv.inputs = aterm.inputs;
    elaborateLegacyOptions(store, drv, true, xpSettings);
    return drv;
}

} // namespace derivation

template void elaborateLegacyOptions(
    const StoreDirConfig & store,
    derivation::Derivation<StorePath> & drv,
    bool shouldWarn,
    const ExperimentalFeatureSettings & mockXpSettings);
template void elaborateLegacyOptions(
    const StoreDirConfig & store,
    derivation::Derivation<SingleDerivedPath> & drv,
    bool shouldWarn,
    const ExperimentalFeatureSettings & mockXpSettings);

} // namespace nix
