#include "nix/store/derivations.hh"
#include "nix/store/derivation/v1/aterm.hh"
#include "nix/store/derivation/full-inputs.hh"
#include "nix/store/derivation/v1/masked.hh"
#include "nix/store/store-api.hh"
#include "nix/util/json-utils.hh"

#include <nlohmann/json.hpp>

namespace nix::derivation {

JsonFormat parseJsonFormat(uint64_t version)
{
    switch (version) {
    case 4:
        return JsonFormat::V4;
    case 5:
        return JsonFormat::V5;
    default:
        throw Error("unsupported derivation JSON format version %d; supported versions are 4 and 5", version);
    }
}

} // namespace nix::derivation

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

void adl_serializer<nix::derivation::EnvValue>::to_json(json & res, const nix::derivation::EnvValue & var)
{
    if (var.passAsFile) {
        res = nlohmann::json::object();
        res["value"] = var.value;
        res["passAsFile"] = true;
    } else {
        res = var.value;
    }
}

nix::derivation::EnvValue adl_serializer<nix::derivation::EnvValue>::from_json(const json & json)
{
    using namespace nix;
    if (json.is_string())
        return {.value = getString(json)};
    auto & obj = getObject(json);
    return {
        .value = getString(valueAt(obj, "value")),
        .passAsFile = getBoolean(valueAt(obj, "passAsFile")),
    };
}

template<typename Input>
void adl_serializer<nix::derivation::OutputWithOptions<Input, nix::DerivationOutput>>::to_json(
    json & res, const nix::derivation::OutputWithOptions<Input, nix::DerivationOutput> & output)
{
    res = output.output;
    res["checks"] = output.options.checks;
    res["unsafeDiscardReferences"] = output.options.unsafeDiscardReferences;
}

template<typename Input>
nix::derivation::OutputWithOptions<Input, nix::DerivationOutput>
adl_serializer<nix::derivation::OutputWithOptions<Input, nix::DerivationOutput>>::from_json(
    const json & json_, const nix::ExperimentalFeatureSettings & xpSettings)
{
    using namespace nix;
    auto & json = getObject(json_);
    return {
        .output =
            [&] {
                /* The options share the JSON object with the output
                   proper, whose serializer accepts exactly its own
                   keys, so they are removed before it sees them. */
                auto output = json;
                output.erase("checks");
                output.erase("unsafeDiscardReferences");
                return adl_serializer<DerivationOutput>::from_json(output, xpSettings);
            }(),
        .options = {
            .checks = [&]() -> std::optional<derivation::OutputChecks<Input>> {
                if (auto * checks = optionalValueAt(json, "checks"))
                    return ptrToOwned<derivation::OutputChecks<Input>>(getNullable(*checks));
                return std::nullopt;
            }(),
            .unsafeDiscardReferences =
                [&] {
                    auto * b = optionalValueAt(json, "unsafeDiscardReferences");
                    return b && getBoolean(*b);
                }(),
        },
    };
}

template struct adl_serializer<nix::derivation::OutputWithOptions<nix::StorePath, nix::DerivationOutput>>;
template struct adl_serializer<nix::derivation::OutputWithOptions<nix::SingleDerivedPath, nix::DerivationOutput>>;

template<typename Input>
static void derivationToJsonV4(json & res, const nix::derivation::Derivation<Input> & d)
{
    using namespace nix;

    /* Project down to the ATerm shape, which is also the shape of the
       legacy JSON format: plain outputs, and a verbatim environment
       carrying the legacy encodings of the derivation options. The one
       place the legacy JSON format is nicer than the ATerm format is
       that the structured attributes are a first-class field, so they
       are split back out of the `__json` environment variable that
       `lower` produces. */
    auto lowered = derivation::lower(d);
    if (d.structuredAttrs)
        lowered.env.erase(std::string{derivation::StructuredAttrs::envVarName});

    res = lowered;

    res["name"] = d.name;
    res["version"] = static_cast<uint64_t>(derivation::JsonFormat::V4);

    if (d.structuredAttrs)
        res["structuredAttrs"] = d.structuredAttrs->structuredAttrs;
}

template<typename Input>
static void derivationToJsonV5(json & res, const nix::derivation::Derivation<Input> & d)
{
    using namespace nix;

    res = nlohmann::json::object();

    res["name"] = d.name;
    res["version"] = static_cast<uint64_t>(derivation::JsonFormat::V5);

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

    {
        nlohmann::json & envObj = res["env"];
        envObj = nlohmann::json::object();
        for (auto & [name, var] : d.env)
            envObj[name] = var;
    }

    if (d.structuredAttrs)
        res["structuredAttrs"] = d.structuredAttrs->structuredAttrs;

    res["options"] = d.options;
}

template<typename Input>
static void
derivationToJson(json & res, const nix::derivation::Derivation<Input> & d, nix::derivation::JsonFormat format)
{
    using nix::derivation::JsonFormat;

    switch (format) {
    case JsonFormat::V4:
        derivationToJsonV4(res, d);
        break;
    case JsonFormat::V5:
        derivationToJsonV5(res, d);
        break;
    }
}

template<typename Input>
void adl_serializer<nix::derivation::Derivation<Input>>::to_json(
    json & res, const nix::derivation::Derivation<Input> & d)
{
    derivationToJsonV5(res, d);
}

template<typename Input>
nix::derivation::Derivation<Input> adl_serializer<nix::derivation::Derivation<Input>>::from_json(
    const json & _json, const nix::ExperimentalFeatureSettings & xpSettings)
{
    using namespace nix;
    using namespace derivation;

    auto & json = getObject(_json);
    switch (parseJsonFormat(getUnsigned(valueAt(json, "version")))) {
    case JsonFormat::V4:
        /* The legacy format encodes the derivation options in the
           environment variables, and decoding that requires a store —
           see `parseJsonAndValidate`. */
        throw Error(
            "derivation JSON format version 4 cannot be decoded without a store; "
            "use format version 5, or a decoder that takes a store (such as `nix derivation add`)");
    case JsonFormat::V5:
        break;
    }

    derivation::Derivation<Input> res{
        .name = getString(valueAt(json, "name")),
        .outputs =
            [&] {
                decltype(res.outputs) outputs;
                try {
                    for (auto & [outputName, output] : getObject(valueAt(json, "outputs")))
                        outputs.insert_or_assign(
                            outputName,
                            adl_serializer<derivation::OutputWithOptions<Input, DerivationOutput>>::from_json(
                                output, xpSettings));
                } catch (Error & e) {
                    e.addTrace({}, "while reading key 'outputs'");
                    throw;
                }
                return outputs;
            }(),
        .inputs =
            [&] {
                try {
                    return inputsFromJson<std::set<Input>>(valueAt(json, "inputs"), xpSettings);
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
                decltype(res.env) env;
                try {
                    for (auto & [name, var] : getObject(valueAt(json, "env")))
                        env.insert_or_assign(name, adl_serializer<derivation::EnvValue>::from_json(var));
                } catch (Error & e) {
                    e.addTrace({}, "while reading key 'env'");
                    throw;
                }
                return env;
            }(),
        .structuredAttrs = [&]() -> std::optional<StructuredAttrs> {
            if (auto structuredAttrs = get(json, "structuredAttrs"))
                return StructuredAttrs{*structuredAttrs};
            return std::nullopt;
        }(),
    };
    /* Optional for leniency towards minimal (e.g. hand-written) JSON;
       absent means all-default options. */
    if (auto * options = optionalValueAt(json, "options"))
        res.options = *options;
    return res;
}

template struct adl_serializer<nix::BasicDerivation>;
template struct adl_serializer<nix::Derivation>;

} // namespace nlohmann

namespace nix::derivation {

template<typename Input>
nlohmann::json toJSON(const Derivation<Input> & drv, JsonFormat format)
{
    nlohmann::json res;
    nlohmann::derivationToJson(res, drv, format);
    return res;
}

template nlohmann::json toJSON(const Derivation<StorePath> & drv, JsonFormat format);
template nlohmann::json toJSON(const Derivation<SingleDerivedPath> & drv, JsonFormat format);

/**
 * Decode the legacy V4 format: parse into the ATerm shape, and then
 * reuse its `elaborate` to decode the legacy environment-variable
 * encodings of the derivation options — exactly as when reading a
 * `.drv` file.
 */
static Full
fromJsonV4(const StoreDirConfig & store, const nlohmann::json & _json, const ExperimentalFeatureSettings & xpSettings)
{
    auto & json = getObject(_json);

    auto aterm = nlohmann::adl_serializer<TraditionalParsed>::from_json(_json, xpSettings);

    /* Fold the first-class structured attributes back into their
       `__json` environment-variable encoding, so that `elaborate` can
       process the whole environment as it would for a `.drv` file. */
    if (auto structuredAttrs = get(json, "structuredAttrs")) {
        StructuredAttrs::checkKeyNotInUse(aterm.env);
        aterm.env.insert(StructuredAttrs{*structuredAttrs}.unparse());
    }

    return elaborate(aterm, store, getString(valueAt(json, "name")), xpSettings);
}

Full fromJSON(const StoreDirConfig & store, const nlohmann::json & json, const ExperimentalFeatureSettings & xpSettings)
{
    switch (parseJsonFormat(getUnsigned(valueAt(getObject(json), "version")))) {
    case JsonFormat::V4:
        return fromJsonV4(store, json, xpSettings);
    case JsonFormat::V5:
        return nlohmann::adl_serializer<Full>::from_json(json, xpSettings);
    }
    unreachable();
}

Full parseJsonAndValidate(Store & store, const nlohmann::json & json, const ExperimentalFeatureSettings & xpSettings)
{
    auto drv = fromJSON(store, json, xpSettings);

    auto readDerivation = derivation::readInvalid(store);
    fillInOutputPaths(drv, store, readDerivation);

    try {
        checkInvariants(drv, store, readDerivation);
    } catch (Error & e) {
        e.addTrace({}, "while checking derivation from JSON with name '%s'", drv.name);
        throw;
    }

    return drv;
}

} // namespace nix::derivation
