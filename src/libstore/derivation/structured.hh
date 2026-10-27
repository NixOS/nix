#pragma once

#include "nix/store/derivations.hh"
#include "nix/store/derivation/full-inputs.hh"
#include "nix/util/json-utils.hh"

#include <nlohmann/json.hpp>

namespace nix::derivation::structured {

using nlohmann::adl_serializer;
using nlohmann::json;

/**
 * Shared derivation fields for JSON and CBOR. The wire codecs distinguish
 * text from arbitrary bytes, including byte-valued map keys. Only the
 * text-only input and output substructures use a JSON value as a carrier.
 * Changes to this schema must consider both wire format versions.
 */
inline void inputsToJson(json & res, const nix::StorePathSet & inputs)
{
    res = nlohmann::json::array();
    for (auto & input : inputs)
        res.emplace_back(input);
}

inline void inputsToJson(json & res, const nix::derivation::FullInputs & inputs)
{
    using namespace nix;
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

inline void inputsToJson(json & res, const std::set<nix::SingleDerivedPath> & inputs)
{
    using namespace nix::derivation;
    inputsToJson(res, FullInputs::fromSet(inputs));
}

template<typename Inputs>
Inputs inputsFromJson(const json & inputsJson, const nix::ExperimentalFeatureSettings & xpSettings);

template<>
inline nix::StorePathSet
inputsFromJson<nix::StorePathSet>(const json & inputsJson, const nix::ExperimentalFeatureSettings &)
{
    using namespace nix;
    StorePathSet inputSrcs;
    for (auto & input : getArray(inputsJson))
        inputSrcs.insert(input);
    return inputSrcs;
}

template<>
inline nix::derivation::FullInputs inputsFromJson<nix::derivation::FullInputs>(
    const json & inputsJson, const nix::ExperimentalFeatureSettings & xpSettings)
{
    using namespace nix;
    using namespace derivation;

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
inline std::set<nix::SingleDerivedPath> inputsFromJson<std::set<nix::SingleDerivedPath>>(
    const json & inputsJson, const nix::ExperimentalFeatureSettings & xpSettings)
{
    using namespace nix::derivation;
    return inputsFromJson<FullInputs>(inputsJson, xpSettings).toSet();
}

template<typename Inputs, typename Writer>
void write(const Derivation<Inputs> & drv, Writer & writer)
{
    writer.value("name", drv.name);
    json outputs = json::object();
    for (auto & [name, output] : drv.outputs)
        outputs[name] = output;
    writer.value("outputs", outputs);
    json inputs;
    inputsToJson(inputs, drv.inputs);
    writer.value("inputs", inputs);
    writer.value("system", drv.platform);
    writer.bytes("builder", drv.builder);
    writer.byteStrings("args", drv.args);
    writer.byteMap("env", drv.env);
    if (drv.structuredAttrs)
        writer.attrs("structuredAttrs", *drv.structuredAttrs);
}

template<typename Inputs, typename Reader>
Derivation<Inputs> read(Reader & reader, const ExperimentalFeatureSettings & xpSettings)
{
    return Derivation<Inputs>{
        .outputs =
            [&] {
                Outputs<> outputs;
                try {
                    auto value = reader.value("outputs");
                    for (auto & [name, output] : getObject(value))
                        outputs.insert_or_assign(name, adl_serializer<DerivationOutput>::from_json(output, xpSettings));
                } catch (Error & e) {
                    e.addTrace({}, "while reading key 'outputs'");
                    throw;
                }
                return outputs;
            }(),
        .inputs =
            [&] {
                try {
                    return inputsFromJson<Inputs>(reader.value("inputs"), xpSettings);
                } catch (Error & e) {
                    e.addTrace({}, "while reading key 'inputs'");
                    throw;
                }
            }(),
        .platform = getString(reader.value("system")),
        .builder = reader.bytes("builder"),
        .args = reader.byteStrings("args"),
        .env =
            [&] {
                try {
                    return reader.byteMap("env");
                } catch (Error & e) {
                    e.addTrace({}, "while reading key 'env'");
                    throw;
                }
            }(),
        .structuredAttrs = reader.attrs("structuredAttrs"),
        .name = getString(reader.value("name")),
    };
}

} // namespace nix::derivation::structured
