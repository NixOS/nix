#include "nix/store/downstream-placeholder.hh"
#include "nix/store/derivations.hh"
#include "nix/util/json-utils.hh"

namespace nix {

std::string DownstreamPlaceholder::render() const
{
    return "/" + hash.to_string(HashFormat::Nix32, false);
}

DownstreamPlaceholder DownstreamPlaceholder::unknownCaOutput(
    const StorePath & drvPath, OutputNameView outputName, const ExperimentalFeatureSettings & xpSettings)
{
    xpSettings.require(Xp::CaDerivations);
    auto drvNameWithExtension = drvPath.name();
    auto drvName = drvNameWithExtension.substr(0, drvNameWithExtension.size() - 4);
    auto clearText =
        "nix-upstream-output:" + std::string{drvPath.hashPart()} + ":" + outputPathName(drvName, outputName);
    return DownstreamPlaceholder{hashString(HashAlgorithm::SHA256, clearText)};
}

DownstreamPlaceholder DownstreamPlaceholder::unknownDerivation(
    const DownstreamPlaceholder & placeholder,
    OutputNameView outputName,
    const ExperimentalFeatureSettings & xpSettings)
{
    xpSettings.require(
        Xp::DynamicDerivations, [&] { return fmt("placeholder for unknown derivation output '%s'", outputName); });
    auto compressed = compressHash(placeholder.hash, 20);
    auto clearText =
        "nix-computed-output:" + compressed.to_string(HashFormat::Nix32, false) + ":" + std::string{outputName};
    return DownstreamPlaceholder{hashString(HashAlgorithm::SHA256, clearText)};
}

DownstreamPlaceholder DownstreamPlaceholder::fromSingleDerivedPathBuilt(
    const SingleDerivedPath::Built & b, const ExperimentalFeatureSettings & xpSettings)
{
    return std::visit(
        overloaded{
            [&](const SingleDerivedPath::Opaque & o) {
                return DownstreamPlaceholder::unknownCaOutput(o.path, b.output, xpSettings);
            },
            [&](const SingleDerivedPath::Built & b2) {
                return DownstreamPlaceholder::unknownDerivation(
                    DownstreamPlaceholder::fromSingleDerivedPathBuilt(b2, xpSettings), b.output, xpSettings);
            },
        },
        b.drvPath->raw());
}

} // namespace nix

namespace nlohmann {

using namespace nix;

template<typename Item>
DrvRef<Item> adl_serializer<DrvRef<Item>>::from_json(const json & json)
{
    // OutputName case: { "drvPath": "self", "output": <output> }
    if (json.type() == nlohmann::json::value_t::object) {
        auto & obj = getObject(json);
        if (auto * drvPath_ = get(obj, "drvPath")) {
            auto & drvPath = *drvPath_;
            if (drvPath.type() == nlohmann::json::value_t::string && getString(drvPath) == "self") {
                return getString(valueAt(obj, "output"));
            }
        }
    }

    // Input case
    return adl_serializer<Item>::from_json(json);
}

template<typename Item>
void adl_serializer<DrvRef<Item>>::to_json(json & json, const DrvRef<Item> & ref)
{
    std::visit(
        overloaded{
            [&](const OutputName & outputName) {
                json = nlohmann::json::object();
                json["drvPath"] = "self";
                json["output"] = outputName;
            },
            [&](const Item & item) { json = item; },
        },
        ref);
}

template struct adl_serializer<nix::DrvRef<StorePath>>;
template struct adl_serializer<nix::DrvRef<SingleDerivedPath>>;

} // namespace nlohmann
