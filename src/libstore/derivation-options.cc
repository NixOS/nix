#include "nix/store/derivation-options.hh"
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

template<typename Inputs>
using OutputChecksVariant = std::
    variant<derivation::OutputChecks<Inputs>, std::map<std::string, derivation::OutputChecks<Inputs>, std::less<>>>;

template<typename Input>
template<typename Inputs>
StringSet DerivationOptions<Input>::getRequiredSystemFeatures(const DerivationT<Inputs, DerivationOutput> & drv) const
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
template<typename Inputs>
bool DerivationOptions<Input>::useUidRange(const DerivationT<Inputs, DerivationOutput> & drv) const
{
    return getRequiredSystemFeatures(drv).count("uid-range");
}

// Explicit instantiations for member function templates
template StringSet DerivationOptions<StorePath>::getRequiredSystemFeatures(const BasicDerivation &) const;
template StringSet DerivationOptions<StorePath>::getRequiredSystemFeatures(const Derivation &) const;
template StringSet DerivationOptions<SingleDerivedPath>::getRequiredSystemFeatures(const Derivation &) const;

template bool DerivationOptions<StorePath>::useUidRange(const BasicDerivation &) const;
template bool DerivationOptions<SingleDerivedPath>::useUidRange(const Derivation &) const;

template struct DerivationOptions<StorePath>;
template struct DerivationOptions<SingleDerivedPath>;

} // namespace nix

namespace nlohmann {

template<typename Inputs>
static nix::DerivationOptions<Inputs> derivationOptionsFromJson(const nlohmann::json & json_)
{
    using namespace nix;
    using namespace derivation;

    auto & json = getObject(json_);

    return {
        .outputChecks = [&]() -> OutputChecksVariant<Inputs> {
            auto outputChecks = getObject(valueAt(json, "outputChecks"));

            auto forAllOutputsOpt = get(outputChecks, "forAllOutputs");
            auto perOutputOpt = get(outputChecks, "perOutput");

            if (forAllOutputsOpt && !perOutputOpt) {
                return static_cast<OutputChecks<Inputs>>(*forAllOutputsOpt);
            } else if (perOutputOpt && !forAllOutputsOpt) {
                return static_cast<std::map<std::string, OutputChecks<Inputs>, std::less<>>>(*perOutputOpt);
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

template<typename Inputs>
static void derivationOptionsToJson(nlohmann::json & json, const nix::DerivationOptions<Inputs> & o)
{
    using namespace nix;
    using namespace derivation;

    json["outputChecks"] = std::visit(
        overloaded{
            [&](const OutputChecks<Inputs> & checks) {
                nlohmann::json outputChecks;
                outputChecks["forAllOutputs"] = checks;
                return outputChecks;
            },
            [&](const std::map<std::string, OutputChecks<Inputs>, std::less<>> & checksPerOutput) {
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

template<typename Inputs>
static nix::derivation::OutputChecks<Inputs> outputChecksFromJson(const nlohmann::json & json_)
{
    using namespace nix;

    auto & json = getObject(json_);

    return {
        .ignoreSelfRefs = getBoolean(valueAt(json, "ignoreSelfRefs")),
        .maxSize = ptrToOwned<uint64_t>(getNullable(valueAt(json, "maxSize"))),
        .maxClosureSize = ptrToOwned<uint64_t>(getNullable(valueAt(json, "maxClosureSize"))),
        .allowedReferences = ptrToOwned<std::set<DrvRef<Inputs>>>(getNullable(valueAt(json, "allowedReferences"))),
        .disallowedReferences = valueAt(json, "disallowedReferences"),
        .allowedRequisites = ptrToOwned<std::set<DrvRef<Inputs>>>(getNullable(valueAt(json, "allowedRequisites"))),
        .disallowedRequisites = valueAt(json, "disallowedRequisites"),
    };
}

template<typename Inputs>
static void outputChecksToJson(nlohmann::json & json, const nix::derivation::OutputChecks<Inputs> & c)
{
    json["ignoreSelfRefs"] = c.ignoreSelfRefs;
    json["maxSize"] = c.maxSize;
    json["maxClosureSize"] = c.maxClosureSize;
    json["allowedReferences"] = c.allowedReferences;
    json["disallowedReferences"] = c.disallowedReferences;
    json["allowedRequisites"] = c.allowedRequisites;
    json["disallowedRequisites"] = c.disallowedRequisites;
}

nix::DerivationOptions<nix::SingleDerivedPath>
adl_serializer<nix::DerivationOptions<nix::SingleDerivedPath>>::from_json(const json & json_)
{
    return derivationOptionsFromJson<nix::SingleDerivedPath>(json_);
}

void adl_serializer<nix::DerivationOptions<nix::SingleDerivedPath>>::to_json(
    json & json, const nix::DerivationOptions<nix::SingleDerivedPath> & o)
{
    derivationOptionsToJson<nix::SingleDerivedPath>(json, o);
}

nix::DerivationOptions<nix::StorePath>
adl_serializer<nix::DerivationOptions<nix::StorePath>>::from_json(const json & json_)
{
    return derivationOptionsFromJson<nix::StorePath>(json_);
}

void adl_serializer<nix::DerivationOptions<nix::StorePath>>::to_json(
    json & json, const nix::DerivationOptions<nix::StorePath> & o)
{
    derivationOptionsToJson<nix::StorePath>(json, o);
}

nix::derivation::OutputChecks<nix::SingleDerivedPath>
adl_serializer<nix::derivation::OutputChecks<nix::SingleDerivedPath>>::from_json(const json & json_)
{
    return outputChecksFromJson<nix::SingleDerivedPath>(json_);
}

void adl_serializer<nix::derivation::OutputChecks<nix::SingleDerivedPath>>::to_json(
    json & json, const nix::derivation::OutputChecks<nix::SingleDerivedPath> & c)
{
    outputChecksToJson<nix::SingleDerivedPath>(json, c);
}

nix::derivation::OutputChecks<nix::StorePath>
adl_serializer<nix::derivation::OutputChecks<nix::StorePath>>::from_json(const json & json_)
{
    return outputChecksFromJson<nix::StorePath>(json_);
}

void adl_serializer<nix::derivation::OutputChecks<nix::StorePath>>::to_json(
    json & json, const nix::derivation::OutputChecks<nix::StorePath> & c)
{
    outputChecksToJson<nix::StorePath>(json, c);
}

} // namespace nlohmann
