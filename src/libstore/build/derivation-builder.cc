#include "nix/util/json-utils.hh"
#include "nix/store/build/derivation-builder.hh"

namespace nlohmann {

nix::ExternalBuilder adl_serializer<nix::ExternalBuilder>::from_json(const json & json)
{
    using namespace nix;
    auto obj = getObject(json);
    return {
        .systems = valueAt(obj, "systems"),
        .program = valueAt(obj, "program"),
        .args = valueAt(obj, "args"),
    };
}

void adl_serializer<nix::ExternalBuilder>::to_json(json & json, const nix::ExternalBuilder & eb)
{
    json = {
        {"systems", eb.systems},
        {"program", eb.program},
        {"args", eb.args},
    };
}

} // namespace nlohmann

namespace nix {

void BuilderFailureError::anchor() {}

void DerivationBuilder::anchor() {}

DerivationBuilderCallbacks::~DerivationBuilderCallbacks() {}

} // namespace nix
