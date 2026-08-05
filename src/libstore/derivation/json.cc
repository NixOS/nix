#include "nix/store/derivations.hh"
#include "nix/store/derivation/aterm.hh"
#include "nix/store/derivation/full-inputs.hh"
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

void adl_serializer<nix::DerivationOutput>::to_json(json & res, const nix::DerivationOutput & o)
{
    using namespace nix;
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

nix::DerivationOutput adl_serializer<nix::DerivationOutput>::from_json(
    const json & _json, const nix::ExperimentalFeatureSettings & xpSettings)
{
    using namespace nix;
    std::set<std::string_view> keys;
    auto & json = getObject(_json);

    for (const auto & [key, _] : json)
        keys.insert(key);

    /* These keys belong to `OutputWithOptions`, which shares the JSON
       object with the output proper. */
    keys.erase("checks");
    keys.erase("unsafeDiscardReferences");

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

static void inputsToJson(json & res, const nix::StorePathSet & inputs)
{
    res = nlohmann::json::array();
    for (auto & input : inputs)
        res.emplace_back(input);
}

static void inputsToJson(json & res, const nix::derivation::FullInputs & inputs)
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

static void inputsToJson(json & res, const std::set<nix::SingleDerivedPath> & inputs)
{
    using namespace nix::derivation;
    inputsToJson(res, FullInputs::fromSet(inputs));
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
        .output = adl_serializer<DerivationOutput>::from_json(json_, xpSettings),
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

/**
 * The fields common to all JSON format versions.
 */
template<typename Input>
static void
derivationToJsonCommon(json & res, const nix::derivation::Derivation<Input> & d, nix::derivation::JsonFormat format)
{
    res = nlohmann::json::object();

    res["name"] = d.name;
    res["version"] = static_cast<uint64_t>(format);

    inputsToJson(res["inputs"], d.inputs);

    res["system"] = d.platform;
    res["builder"] = d.builder;
    res["args"] = d.args;

    if (d.structuredAttrs)
        res["structuredAttrs"] = d.structuredAttrs->structuredAttrs;
}

template<typename Input>
static void derivationToJsonV4(json & res, const nix::derivation::Derivation<Input> & d)
{
    using namespace nix;

    derivationToJsonCommon(res, d, derivation::JsonFormat::V4);

    /* Project down to the ATerm shape, which is also the shape of the
       legacy JSON format: plain outputs, and a verbatim environment
       carrying the legacy encodings of the derivation options. The one
       place the legacy JSON format is nicer than the ATerm format is
       that the structured attributes are a first-class field, so they
       are split back out of the `__json` environment variable that
       `lower` produces. */
    auto lowered = [&] {
        if constexpr (std::is_same_v<Input, SingleDerivedPath>)
            return derivation::ATerm::lower(d);
        else
            return derivation::BasicATerm::lower(d);
    }();

    {
        nlohmann::json & outputsObj = res["outputs"];
        outputsObj = nlohmann::json::object();
        for (auto & [outputName, output] : lowered.outputs)
            outputsObj[outputName] = output;
    }

    if (d.structuredAttrs)
        lowered.env.erase(std::string{StructuredAttrs::envVarName});
    res["env"] = lowered.env;
}

template<typename Input>
static void derivationToJsonV5(json & res, const nix::derivation::Derivation<Input> & d)
{
    using namespace nix;

    derivationToJsonCommon(res, d, derivation::JsonFormat::V5);

    {
        nlohmann::json & outputsObj = res["outputs"];
        outputsObj = nlohmann::json::object();
        for (auto & [outputName, output] : d.outputs)
            outputsObj[outputName] = output;
    }

    {
        nlohmann::json & envObj = res["env"];
        envObj = nlohmann::json::object();
        for (auto & [name, var] : d.env)
            envObj[name] = var;
    }

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

template<typename Inputs>
static Inputs inputsFromJson(const json & inputsJson, const nix::ExperimentalFeatureSettings & xpSettings);

template<>
nix::StorePathSet inputsFromJson<nix::StorePathSet>(const json & inputsJson, const nix::ExperimentalFeatureSettings &)
{
    using namespace nix;
    StorePathSet inputSrcs;
    for (auto & input : getArray(inputsJson))
        inputSrcs.insert(input);
    return inputSrcs;
}

template<>
nix::derivation::FullInputs inputsFromJson<nix::derivation::FullInputs>(
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
std::set<nix::SingleDerivedPath> inputsFromJson<std::set<nix::SingleDerivedPath>>(
    const json & inputsJson, const nix::ExperimentalFeatureSettings & xpSettings)
{
    using namespace nix::derivation;
    return inputsFromJson<FullInputs>(inputsJson, xpSettings).toSet();
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

    ATerm aterm{
        .outputs =
            [&] {
                Outputs<Output> outputs;
                try {
                    for (auto & [outputName, output] : getObject(valueAt(json, "outputs")))
                        outputs.insert_or_assign(
                            outputName, nlohmann::adl_serializer<Output>::from_json(output, xpSettings));
                } catch (Error & e) {
                    e.addTrace({}, "while reading key 'outputs'");
                    throw;
                }
                return outputs;
            }(),
        .inputs =
            [&] {
                try {
                    return nlohmann::inputsFromJson<FullInputs>(valueAt(json, "inputs"), xpSettings);
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
                StringPairs env;
                try {
                    for (auto & [name, value] : getObject(valueAt(json, "env")))
                        env.insert_or_assign(name, getString(value));
                } catch (Error & e) {
                    e.addTrace({}, "while reading key 'env'");
                    throw;
                }
                return env;
            }(),
    };

    /* Fold the first-class structured attributes back into their
       `__json` environment-variable encoding, so that `elaborate` can
       process the whole environment as it would for a `.drv` file. */
    if (auto structuredAttrs = get(json, "structuredAttrs")) {
        StructuredAttrs::checkKeyNotInUse(aterm.env);
        aterm.env.insert(StructuredAttrs{*structuredAttrs}.unparse());
    }

    return aterm.elaborate(store, getString(valueAt(json, "name")), xpSettings);
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

    fillInOutputPaths(drv, store);

    try {
        checkInvariants(drv, store);
    } catch (Error & e) {
        e.addTrace({}, "while checking derivation from JSON with name '%s'", drv.name);
        throw;
    }

    return drv;
}

} // namespace nix::derivation
