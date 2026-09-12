#include "nix/store/derivations.hh"
#include "nix/store/derivation/full-inputs.hh"
#include "nix/store/derivation/masked.hh"
#include "nix/store/store-api.hh"
#include "nix/util/json-utils.hh"

#include <nlohmann/json.hpp>

namespace nlohmann {

/* One serializer per output alternative, with the `Output` variant
   dispatching into them --- the same shape as the `ATermSerializer`
   specializations for outputs on the ATerm side. Each of the alternatives
   validates the whole object rather than trusting the dispatcher to
   have chosen correctly, so any of them is safe to call on its own,
   which is what the masked forms do: their type says that every output
   is one particular alternative. */

void adl_serializer<nix::derivation::Output::InputAddressed>::to_json(
    json & res, const nix::derivation::Output::InputAddressed & o)
{
    res = json::object();
    res["path"] = o.path;
}

void adl_serializer<nix::derivation::Output::CAFixed>::to_json(json & res, const nix::derivation::Output::CAFixed & o)
{
    res = o.ca;
    // FIXME print refs?
    /* it would be nice to output the path for user convenience, but
       this would require us to know the store dir. */
#if 0
    res["path"] = o.path(store, drvName, outputName);
#endif
}

void adl_serializer<nix::derivation::Output::CAFloating>::to_json(
    json & res, const nix::derivation::Output::CAFloating & o)
{
    using namespace nix;
    res = json::object();
    res["method"] = std::string{o.method.render()};
    res["hashAlgo"] = printHashAlgo(o.hashAlgo);
}

void adl_serializer<nix::derivation::Output::Deferred>::to_json(json & res, const nix::derivation::Output::Deferred &)
{
    res = json::object();
}

void adl_serializer<nix::derivation::Output::Impure>::to_json(json & res, const nix::derivation::Output::Impure & o)
{
    using namespace nix;
    res = json::object();
    res["method"] = std::string{o.method.render()};
    res["hashAlgo"] = printHashAlgo(o.hashAlgo);
    res["impure"] = true;
}

void adl_serializer<nix::DerivationOutput>::to_json(json & res, const nix::DerivationOutput & o)
{
    std::visit([&](const auto & alt) { res = alt; }, o.raw);
}

/**
 * The keys an output object has, which is what distinguishes the
 * alternatives from each other.
 */
static std::set<std::string_view> outputKeys(const nlohmann::json::object_t & json)
{
    std::set<std::string_view> keys;
    for (const auto & [key, _] : json)
        keys.insert(key);
    return keys;
}

/**
 * The `method` and `hashAlgo` fields, which the two content-addressing
 * alternatives that do not have a fixed content address share.
 */
static std::pair<nix::ContentAddressMethod, nix::HashAlgorithm>
parseMethodAlgo(const nlohmann::json::object_t & json, const nix::ExperimentalFeatureSettings & xpSettings)
{
    using namespace nix;
    ContentAddressMethod method = ContentAddressMethod::parse(getString(valueAt(json, "method")));
    if (method == ContentAddressMethod::Raw::Text)
        xpSettings.require(Xp::DynamicDerivations, "text-hashed derivation output in JSON");

    auto hashAlgo = parseHashAlgo(getString(valueAt(json, "hashAlgo")));
    return {std::move(method), std::move(hashAlgo)};
}

nix::derivation::Output::InputAddressed adl_serializer<nix::derivation::Output::InputAddressed>::from_json(
    const json & _json, const nix::ExperimentalFeatureSettings &)
{
    using namespace nix;
    auto & json = getObject(_json);
    if (outputKeys(json) != std::set<std::string_view>{"path"})
        throw Error("invalid JSON for input-addressed derivation output");
    return {
        .path = valueAt(json, "path"),
    };
}

nix::derivation::Output::CAFixed adl_serializer<nix::derivation::Output::CAFixed>::from_json(
    const json & _json, const nix::ExperimentalFeatureSettings & xpSettings)
{
    using namespace nix;
    auto & json = getObject(_json);
    if (outputKeys(json) != std::set<std::string_view>{"method", "hash"})
        throw Error("invalid JSON for fixed content-addressing derivation output");
    derivation::Output::CAFixed dof{
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

nix::derivation::Output::CAFloating adl_serializer<nix::derivation::Output::CAFloating>::from_json(
    const json & _json, const nix::ExperimentalFeatureSettings & xpSettings)
{
    using namespace nix;
    auto & json = getObject(_json);
    if (outputKeys(json) != std::set<std::string_view>{"method", "hashAlgo"})
        throw Error("invalid JSON for floating content-addressing derivation output");
    xpSettings.require(Xp::CaDerivations);
    auto [method, hashAlgo] = parseMethodAlgo(json, xpSettings);
    return {
        .method = std::move(method),
        .hashAlgo = std::move(hashAlgo),
    };
}

nix::derivation::Output::Deferred adl_serializer<nix::derivation::Output::Deferred>::from_json(
    const json & _json, const nix::ExperimentalFeatureSettings &)
{
    using namespace nix;
    if (!getObject(_json).empty())
        throw Error("invalid JSON for deferred derivation output");
    return {};
}

nix::derivation::Output::Impure adl_serializer<nix::derivation::Output::Impure>::from_json(
    const json & _json, const nix::ExperimentalFeatureSettings & xpSettings)
{
    using namespace nix;
    auto & json = getObject(_json);
    if (outputKeys(json) != std::set<std::string_view>{"method", "hashAlgo", "impure"})
        throw Error("invalid JSON for impure derivation output");
    xpSettings.require(Xp::ImpureDerivations);
    auto [method, hashAlgo] = parseMethodAlgo(json, xpSettings);
    return {
        .method = std::move(method),
        .hashAlgo = hashAlgo,
    };
}

nix::DerivationOutput adl_serializer<nix::DerivationOutput>::from_json(
    const json & _json, const nix::ExperimentalFeatureSettings & xpSettings)
{
    using namespace nix;
    auto & json = getObject(_json);
    auto keys = outputKeys(json);

    using namespace nix::derivation;

    if (keys == std::set<std::string_view>{"path"})
        return adl_serializer<Output::InputAddressed>::from_json(_json, xpSettings);

    else if (keys == std::set<std::string_view>{"method", "hash"})
        return adl_serializer<Output::CAFixed>::from_json(_json, xpSettings);

    else if (keys == std::set<std::string_view>{"method", "hashAlgo"})
        return adl_serializer<Output::CAFloating>::from_json(_json, xpSettings);

    else if (keys == std::set<std::string_view>{})
        return adl_serializer<Output::Deferred>::from_json(_json, xpSettings);

    else if (keys == std::set<std::string_view>{"method", "hashAlgo", "impure"})
        return adl_serializer<Output::Impure>::from_json(_json, xpSettings);

    else
        throw Error("invalid JSON for derivation output");
}

void adl_serializer<nix::derivation::FullInputs>::to_json(json & res, const nix::derivation::FullInputs & inputs)
{
    using namespace nix;
    res = json::object();

    res["srcs"] = inputs.srcs;

    auto doInput = [&](this const auto & doInput, const auto & inputNode) -> json {
        auto value = json::object();
        value["outputs"] = inputNode.value;
        {
            auto next = json::object();
            for (auto & [outputId, childNode] : inputNode.childMap)
                next[outputId] = doInput(childNode);
            value["dynamicOutputs"] = std::move(next);
        }
        return value;
    };

    auto & inputDrvsObj = res["drvs"];
    inputDrvsObj = json::object();
    for (auto & [inputDrv, inputNode] : inputs.drvs.map)
        inputDrvsObj[inputDrv->to_string()] = doInput(inputNode);
}

nix::derivation::FullInputs adl_serializer<nix::derivation::FullInputs>::from_json(
    const json & _json, const nix::ExperimentalFeatureSettings & xpSettings)
{
    using namespace nix;
    using namespace derivation;

    auto & inputsObj = getObject(_json);
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
        for (auto & [inputDrvPath, inputOutputs] : getObject(valueAt(inputsObj, "drvs"))) {
            auto node = doInput(inputOutputs);
            /* Such an entry cannot be represented in the flat inputs
               set, nor written as ATerm, whose reader rejects it too. */
            if (node.value.empty() && node.childMap.empty())
                throw FormatError("inputDrvs entry for '%s' specifies no outputs", inputDrvPath);
            inputs.drvs.map[DerivationPath::require(StorePath{inputDrvPath})] = std::move(node);
        }
    } catch (Error & e) {
        e.addTrace({}, "while reading key 'drvs'");
        throw;
    }

    return inputs;
}

/**
 * The masked form names its input derivations by hash rather than by
 * store path, and has no nesting: it is only ever constructed once
 * dynamic derivations have been resolved away.
 *
 * The keys are base-16, as in the ATerm encoding, so that the two
 * renderings of the same value agree on how a hash is spelled.
 */
void adl_serializer<nix::derivation::masked::HashInputs>::to_json(
    json & res, const nix::derivation::masked::HashInputs & inputs)
{
    using namespace nix;
    res = json::object();

    res["srcs"] = inputs.srcs;

    auto & inputDrvsObj = res["drvs"];
    inputDrvsObj = json::object();
    for (auto & [drvHash, outputNames] : inputs.drvs.map)
        inputDrvsObj[drvHash.to_string(HashFormat::Base16, false)] = outputNames;
}

nix::derivation::masked::HashInputs adl_serializer<nix::derivation::masked::HashInputs>::from_json(
    const json & _json, const nix::ExperimentalFeatureSettings &)
{
    using namespace nix;
    using namespace derivation;

    auto & inputsObj = getObject(_json);
    masked::HashInputs inputs;

    try {
        for (auto & input : getArray(valueAt(inputsObj, "srcs")))
            inputs.srcs.insert(input);
    } catch (Error & e) {
        e.addTrace({}, "while reading key 'srcs'");
        throw;
    }

    try {
        for (auto & [drvHash, outputNames] : getObject(valueAt(inputsObj, "drvs"))) {
            auto hash = Hash::parseExplicitFormatUnprefixed(drvHash, HashAlgorithm::SHA256, HashFormat::Base16);
            /* As in the ATerm encoding, each hash has one spelling.
               Base-16 decoding also accepts upper-case digits, and two
               spellings of one hash would otherwise collapse into a
               single entry here. */
            if (hash.to_string(HashFormat::Base16, false) != drvHash)
                throw FormatError("input derivation hash '%s' is not in lower-case base-16", drvHash);
            auto outputs = getStringSet(outputNames);
            if (outputs.empty())
                throw FormatError("inputDrvs entry for '%s' specifies no outputs", drvHash);
            inputs.drvs.map.insert_or_assign(std::move(hash), std::move(outputs));
        }
    } catch (Error & e) {
        e.addTrace({}, "while reading key 'drvs'");
        throw;
    }

    return inputs;
}

/**
 * `FullInputs` and `masked::HashInputs` have serializers of their own,
 * and a basic derivation's `StorePathSet` is just an array, which
 * `nlohmann` already renders. Only the flat set of deriving paths needs
 * saying here: `SingleDerivedPath` has a JSON rendering of its own, and
 * an array of those is not this format, so that shape is converted
 * through `FullInputs` rather than given a serializer that would
 * contradict the element type's.
 */
template<typename Inputs>
static json inputsToJson(const Inputs & inputs)
{
    using namespace nix::derivation;
    if constexpr (std::is_same_v<Inputs, std::set<nix::SingleDerivedPath>>)
        return FullInputs::fromSet(inputs);
    else
        return inputs;
}

template<typename Inputs>
static Inputs inputsFromJson(const json & json, const nix::ExperimentalFeatureSettings & xpSettings)
{
    using namespace nix::derivation;
    if constexpr (std::is_same_v<Inputs, std::set<nix::SingleDerivedPath>>)
        return adl_serializer<FullInputs>::from_json(json, xpSettings).toSet();
    else if constexpr (std::is_same_v<Inputs, nix::StorePathSet>) {
        /* Element by element, so that a malformed value is a `nix::Error`
           like every other, which the caller can add a trace to. */
        nix::StorePathSet inputSrcs;
        for (auto & input : nix::getArray(json))
            inputSrcs.insert(input);
        return inputSrcs;
    } else
        return adl_serializer<Inputs>::from_json(json, xpSettings);
}

template<typename Inputs, typename Out>
void adl_serializer<nix::derivation::Derivation<Inputs, Out>>::to_json(
    json & res, const nix::derivation::Derivation<Inputs, Out> & d)
{
    using namespace nix;
    res = nlohmann::json::object();

    res["name"] = d.name;
    res["version"] = expectedJsonVersionDerivation;

    {
        nlohmann::json & outputsObj = res["outputs"];
        outputsObj = nlohmann::json::object();
        for (auto & [outputName, output] : d.outputs)
            outputsObj[outputName] = output;
    }

    res["inputs"] = inputsToJson(d.inputs);

    res["system"] = d.platform;
    res["builder"] = d.builder;
    res["args"] = d.args;
    res["env"] = d.env;

    if (d.structuredAttrs)
        res["structuredAttrs"] = d.structuredAttrs->structuredAttrs;
}

template<typename Inputs, typename Out>
nix::derivation::Derivation<Inputs, Out> adl_serializer<nix::derivation::Derivation<Inputs, Out>>::from_json(
    const json & _json, const nix::ExperimentalFeatureSettings & xpSettings)
{
    using namespace nix;
    using namespace derivation;

    auto & json = getObject(_json);
    {
        auto version = getUnsigned(valueAt(json, "version"));
        if (version != expectedJsonVersionDerivation)
            throw Error(
                "Unsupported derivation JSON format version %d, only format version %d is currently supported.",
                version,
                expectedJsonVersionDerivation);
    }

    return derivation::Derivation<Inputs, Out>{
        .outputs =
            [&] {
                Outputs<Out> outputs;
                try {
                    for (auto & [outputName, output] : getObject(valueAt(json, "outputs")))
                        outputs.insert_or_assign(outputName, adl_serializer<Out>::from_json(output, xpSettings));
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
        .name = getString(valueAt(json, "name")),
    };
}

template struct adl_serializer<nix::BasicDerivation>;
template struct adl_serializer<nix::Derivation>;

/* The masked forms, which `masked.cc` hashes. */
template struct adl_serializer<nix::derivation::masked::Drv<nix::derivation::Output::Deferred>>;
template struct adl_serializer<nix::derivation::masked::Drv<nix::derivation::Output::InputAddressed>>;

} // namespace nlohmann
