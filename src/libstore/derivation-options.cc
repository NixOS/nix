#include "nix/store/derivation-options.hh"
#include "nix/util/json-utils.hh"
#include "nix/store/worker-settings.hh"
#include "nix/store/derivations.hh"
#include "nix/store/derived-path.hh"
#include "nix/util/types.hh"
#include "nix/util/util.hh"

#include <optional>
#include <string>
#include <variant>

namespace nix::derivation {

template<typename Input>
bool TopOptions<Input>::substitutesAllowed(const WorkerSettings & workerSettings) const
{
    return workerSettings.alwaysAllowSubstitutes ? true : allowSubstitutes;
}

template struct TopOptions<StorePath>;
template struct TopOptions<SingleDerivedPath>;

} // namespace nix::derivation

namespace nlohmann {

template<typename Inputs>
static nix::derivation::TopOptions<Inputs> derivationOptionsFromJson(const nlohmann::json & json_)
{
    using namespace nix;

    auto & json = getObject(json_);

    return {
        .allOutputChecks = ptrToOwned<derivation::OutputChecks<Inputs>>(getNullable(valueAt(json, "allOutputChecks"))),

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
static void derivationOptionsToJson(nlohmann::json & json, const nix::derivation::TopOptions<Inputs> & o)
{
    using namespace nix;

    json["allOutputChecks"] = o.allOutputChecks;

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

template<typename Inputs>
static nix::derivation::OutputOptions<Inputs> outputOptionsFromJson(const nlohmann::json & json_)
{
    using namespace nix;

    auto & json = getObject(json_);

    return {
        .checks = ptrToOwned<derivation::OutputChecks<Inputs>>(getNullable(valueAt(json, "checks"))),
        .unsafeDiscardReferences = getBoolean(valueAt(json, "unsafeDiscardReferences")),
    };
}

template<typename Inputs>
static void outputOptionsToJson(nlohmann::json & json, const nix::derivation::OutputOptions<Inputs> & o)
{
    json["checks"] = o.checks;
    json["unsafeDiscardReferences"] = o.unsafeDiscardReferences;
}

nix::derivation::OutputOptions<nix::SingleDerivedPath>
adl_serializer<nix::derivation::OutputOptions<nix::SingleDerivedPath>>::from_json(const json & json_)
{
    return outputOptionsFromJson<nix::SingleDerivedPath>(json_);
}

void adl_serializer<nix::derivation::OutputOptions<nix::SingleDerivedPath>>::to_json(
    json & json, const nix::derivation::OutputOptions<nix::SingleDerivedPath> & o)
{
    outputOptionsToJson<nix::SingleDerivedPath>(json, o);
}

nix::derivation::OutputOptions<nix::StorePath>
adl_serializer<nix::derivation::OutputOptions<nix::StorePath>>::from_json(const json & json_)
{
    return outputOptionsFromJson<nix::StorePath>(json_);
}

void adl_serializer<nix::derivation::OutputOptions<nix::StorePath>>::to_json(
    json & json, const nix::derivation::OutputOptions<nix::StorePath> & o)
{
    outputOptionsToJson<nix::StorePath>(json, o);
}

nix::derivation::TopOptions<nix::SingleDerivedPath>
adl_serializer<nix::derivation::TopOptions<nix::SingleDerivedPath>>::from_json(const json & json_)
{
    return derivationOptionsFromJson<nix::SingleDerivedPath>(json_);
}

void adl_serializer<nix::derivation::TopOptions<nix::SingleDerivedPath>>::to_json(
    json & json, const nix::derivation::TopOptions<nix::SingleDerivedPath> & o)
{
    derivationOptionsToJson<nix::SingleDerivedPath>(json, o);
}

nix::derivation::TopOptions<nix::StorePath>
adl_serializer<nix::derivation::TopOptions<nix::StorePath>>::from_json(const json & json_)
{
    return derivationOptionsFromJson<nix::StorePath>(json_);
}

void adl_serializer<nix::derivation::TopOptions<nix::StorePath>>::to_json(
    json & json, const nix::derivation::TopOptions<nix::StorePath> & o)
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
