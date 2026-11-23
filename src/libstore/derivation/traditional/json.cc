#include "nix/store/derivation/traditional.hh"
#include "nix/store/derivation/full-inputs.hh"
#include "nix/store/derivation/masked.hh"
#include "nix/util/json-utils.hh"

#include <nlohmann/json.hpp>

namespace nlohmann {

/**
 * `FullInputs` and `masked::HashInputs` have serializers of their own,
 * and a basic derivation's `StorePathSet` is just an array.
 */
template<typename Inputs>
static Inputs inputsFromJson(const json & json, const nix::ExperimentalFeatureSettings & xpSettings)
{
    if constexpr (std::is_same_v<Inputs, nix::StorePathSet>) {
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
void adl_serializer<nix::derivation::TraditionalParsedT<Inputs, Out>>::to_json(
    json & res, const nix::derivation::TraditionalParsedT<Inputs, Out> & a)
{
    res = nlohmann::json::object();

    {
        nlohmann::json & outputsObj = res["outputs"];
        outputsObj = nlohmann::json::object();
        for (auto & [outputName, output] : a.outputs)
            outputsObj[outputName] = output;
    }

    res["inputs"] = a.inputs;

    res["system"] = a.platform;
    res["builder"] = a.builder;
    res["args"] = a.args;
    res["env"] = a.env;
}

template<typename Inputs, typename Out>
nix::derivation::TraditionalParsedT<Inputs, Out>
adl_serializer<nix::derivation::TraditionalParsedT<Inputs, Out>>::from_json(
    const json & _json, const nix::ExperimentalFeatureSettings & xpSettings)
{
    using namespace nix;
    using namespace derivation;

    auto & json = getObject(_json);

    return {
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
}

/* The shapes the legacy format lowers to, and the masked forms, which
   `masked.cc` hashes. */
template struct adl_serializer<nix::derivation::TraditionalParsed>;
template struct adl_serializer<nix::derivation::BasicTraditionalParsed>;
template struct adl_serializer<nix::derivation::masked::Drv<nix::derivation::Output::Deferred>>;
template struct adl_serializer<nix::derivation::masked::Drv<nix::derivation::Output::InputAddressed>>;

} // namespace nlohmann
