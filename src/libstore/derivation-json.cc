#include "nix/store/derivations.hh"
#include "nix/store/derivation/full-inputs.hh"
#include "nix/store/store-api.hh"
#include "nix/util/json-utils.hh"

#include <nlohmann/json.hpp>

namespace nlohmann {

using namespace nix;

void adl_serializer<DerivationOutput>::to_json(json & res, const DerivationOutput & o)
{
    res = nlohmann::json::object();
    std::visit(
        overloaded{
            [&](const DerivationOutput::InputAddressed & doi) { res["path"] = doi.path; },
            [&](const DerivationOutput::CAFixed & dof) {
                res = dof.ca;
        // FIXME print refs?
        /* it would be nice to output the path for user convenience, but
           this would require us to know the store dir. */
#if 0
                res["path"] = dof.path(store, drvName, outputName);
#endif
            },
            [&](const DerivationOutput::CAFloating & dof) {
                res["method"] = std::string{dof.method.render()};
                res["hashAlgo"] = printHashAlgo(dof.hashAlgo);
            },
            [&](const DerivationOutput::Deferred &) {},
            [&](const DerivationOutput::Impure & doi) {
                res["method"] = std::string{doi.method.render()};
                res["hashAlgo"] = printHashAlgo(doi.hashAlgo);
                res["impure"] = true;
            },
        },
        o.raw);
}

DerivationOutput
adl_serializer<DerivationOutput>::from_json(const json & _json, const ExperimentalFeatureSettings & xpSettings)
{
    std::set<std::string_view> keys;
    auto & json = getObject(_json);

    for (const auto & [key, _] : json)
        keys.insert(key);

    auto methodAlgo = [&]() -> std::pair<ContentAddressMethod, HashAlgorithm> {
        ContentAddressMethod method = ContentAddressMethod::parse(getString(valueAt(json, "method")));
        if (method == ContentAddressMethod::Raw::Text)
            xpSettings.require(Xp::DynamicDerivations, "text-hashed derivation output in JSON");

        auto hashAlgo = parseHashAlgo(getString(valueAt(json, "hashAlgo")));
        return {std::move(method), std::move(hashAlgo)};
    };

    if (keys == (std::set<std::string_view>{"path"})) {
        return DerivationOutput::InputAddressed{
            .path = valueAt(json, "path"),
        };
    }

    else if (keys == (std::set<std::string_view>{"method", "hash"})) {
        auto dof = DerivationOutput::CAFixed{
            .ca = static_cast<ContentAddress>(_json),
        };
        if (dof.ca.method == ContentAddressMethod::Raw::Text)
            xpSettings.require(Xp::DynamicDerivations, "text-hashed derivation output in JSON");
        /* We no longer produce this (denormalized) field (for the
           reasons described above), so we don't need to check it. */
#if 0
        if (dof.path(store, drvName, outputName) != static_cast<StorePath>(valueAt(json, "path")))
            throw Error("Path doesn't match derivation output");
#endif
        return dof;
    }

    else if (keys == (std::set<std::string_view>{"method", "hashAlgo"})) {
        xpSettings.require(Xp::CaDerivations);
        auto [method, hashAlgo] = methodAlgo();
        return DerivationOutput::CAFloating{
            .method = std::move(method),
            .hashAlgo = std::move(hashAlgo),
        };
    }

    else if (keys == (std::set<std::string_view>{})) {
        return DerivationOutput::Deferred{};
    }

    else if (keys == (std::set<std::string_view>{"method", "hashAlgo", "impure"})) {
        xpSettings.require(Xp::ImpureDerivations);
        auto [method, hashAlgo] = methodAlgo();
        return DerivationOutput::Impure{
            .method = std::move(method),
            .hashAlgo = hashAlgo,
        };
    }

    else {
        throw Error("invalid JSON for derivation output");
    }
}

static void inputsToJson(json & res, const StorePathSet & inputs)
{
    res = nlohmann::json::array();
    for (auto & input : inputs)
        res.emplace_back(input);
}

static void inputsToJson(json & res, const FullInputs & inputs)
{
    res = nlohmann::json::object();

    inputsToJson(res["srcs"], inputs.srcs);

    auto doInput = [&](this const auto & doInput, const auto & inputNode) -> nlohmann::json {
        auto value = nlohmann::json::object();
        value["outputs"] = inputNode.value;
        {
            auto next = nlohmann::json::object();
            for (auto & [outputId, childNode] : inputNode.childMap)
                next[outputId] = doInput(childNode);
            value["dynamicOutputs"] = std::move(next);
        }
        return value;
    };

    auto & inputDrvsObj = res["drvs"];
    inputDrvsObj = nlohmann::json::object();
    for (auto & [inputDrv, inputNode] : inputs.drvs.map)
        inputDrvsObj[inputDrv.to_string()] = doInput(inputNode);
}

static void inputsToJson(json & res, const std::set<SingleDerivedPath> & inputs)
{
    inputsToJson(res, FullInputs::fromSet(inputs));
}

template<typename Inputs>
void adl_serializer<DerivationT<Inputs>>::to_json(json & res, const DerivationT<Inputs> & d)
{
    res = nlohmann::json::object();

    res["name"] = d.name;
    res["version"] = expectedJsonVersionDerivation;

    {
        nlohmann::json & outputsObj = res["outputs"];
        outputsObj = nlohmann::json::object();
        for (auto & [outputName, output] : d.outputs)
            outputsObj[outputName] = output;
    }

    inputsToJson(res["inputs"], d.inputs);

    res["system"] = d.platform;
    res["builder"] = d.builder;
    res["args"] = d.args;
    res["env"] = d.env;
    res["options"] = d.options;

    if (d.structuredAttrs)
        res["structuredAttrs"] = d.structuredAttrs->structuredAttrs;
}

template<typename Inputs>
static Inputs inputsFromJson(const json & inputsJson, const ExperimentalFeatureSettings & xpSettings);

template<>
StorePathSet inputsFromJson<StorePathSet>(const json & inputsJson, const ExperimentalFeatureSettings &)
{
    StorePathSet inputSrcs;
    for (auto & input : getArray(inputsJson))
        inputSrcs.insert(input);
    return inputSrcs;
}

template<>
FullInputs inputsFromJson<FullInputs>(const json & inputsJson, const ExperimentalFeatureSettings & xpSettings)
{
    auto inputsObj = getObject(inputsJson);
    FullInputs inputs;

    try {
        for (auto & input : getArray(valueAt(inputsObj, "srcs")))
            inputs.srcs.insert(input);
    } catch (Error & e) {
        e.addTrace({}, "while reading key 'srcs'");
        throw;
    }

    try {
        auto doInput = [&](this const auto & doInput, const auto & _json) -> DerivedPathMap<StringSet>::ChildNode {
            auto & json = getObject(_json);
            DerivedPathMap<StringSet>::ChildNode node;
            node.value = getStringSet(valueAt(json, "outputs"));
            for (auto & [outputId, childNode] : getObject(valueAt(json, "dynamicOutputs"))) {
                xpSettings.require(
                    Xp::DynamicDerivations, [&] { return fmt("dynamic output '%s' in JSON", outputId); });
                node.childMap[outputId] = doInput(childNode);
            }
            return node;
        };
        for (auto & [inputDrvPath, inputOutputs] : getObject(valueAt(inputsObj, "drvs")))
            inputs.drvs.map[StorePath{inputDrvPath}] = doInput(inputOutputs);
    } catch (Error & e) {
        e.addTrace({}, "while reading key 'drvs'");
        throw;
    }

    return inputs;
}

template<>
std::set<SingleDerivedPath>
inputsFromJson<std::set<SingleDerivedPath>>(const json & inputsJson, const ExperimentalFeatureSettings & xpSettings)
{
    return inputsFromJson<FullInputs>(inputsJson, xpSettings).toSet();
}

template<typename Inputs>
DerivationT<Inputs>
adl_serializer<DerivationT<Inputs>>::from_json(const json & _json, const ExperimentalFeatureSettings & xpSettings)
{
    auto & json = getObject(_json);
    {
        auto version = getUnsigned(valueAt(json, "version"));
        if (version != expectedJsonVersionDerivation)
            throw Error(
                "Unsupported derivation JSON format version %d, only format version %d is currently supported.",
                version,
                expectedJsonVersionDerivation);
    }

    return DerivationT<Inputs>{
        .outputs =
            [&] {
                DerivationOutputs<> outputs;
                try {
                    for (auto & [outputName, output] : getObject(valueAt(json, "outputs")))
                        outputs.insert_or_assign(
                            outputName, adl_serializer<DerivationOutput>::from_json(output, xpSettings));
                } catch (Error & e) {
                    e.addTrace({}, "while reading key 'outputs'");
                    throw;
                }
                return outputs;
            }(),
        .inputs =
            [&] {
                try {
                    return inputsFromJson<Inputs>(valueAt(json, "inputs"), xpSettings);
                } catch (Error & e) {
                    e.addTrace({}, "while reading key 'inputs'");
                    throw;
                }
            }(),
        .platform = getString(valueAt(json, "system")),
        .builder = getString(valueAt(json, "builder")),
        .args = getStringList(valueAt(json, "args")),
        .env =
            [&] {
                try {
                    return getStringMap(valueAt(json, "env"));
                } catch (Error & e) {
                    e.addTrace({}, "while reading key 'env'");
                    throw;
                }
            }(),
        .structuredAttrs = [&]() -> std::optional<StructuredAttrs> {
            if (auto structuredAttrs = get(json, "structuredAttrs"))
                return StructuredAttrs{*structuredAttrs};
            return std::nullopt;
        }(),
        .options = valueAt(json, "options"),
        .name = getString(valueAt(json, "name")),
    };
}

template struct adl_serializer<BasicDerivation>;
template struct adl_serializer<Derivation>;

} // namespace nlohmann
