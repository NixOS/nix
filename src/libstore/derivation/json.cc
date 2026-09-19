#include "nix/store/derivations.hh"
#include "structured.hh"
#include "nix/store/derivation/full-inputs.hh"
#include "nix/store/store-api.hh"
#include "nix/util/json-utils.hh"

#include <nlohmann/json.hpp>

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

namespace {

struct JsonWriter
{
    json & object;

    void value(const char * key, const json & value)
    {
        object[key] = value;
    }

    void bytes(const char * key, const std::string & value)
    {
        object[key] = value;
    }

    void byteStrings(const char * key, const nix::Strings & value)
    {
        object[key] = value;
    }

    void byteMap(const char * key, const nix::StringPairs & value)
    {
        object[key] = value;
    }

    void attrs(const char * key, const nix::StructuredAttrs & value)
    {
        object[key] = value.structuredAttrs;
    }
};

struct JsonReader
{
    const json::object_t & object;

    const json & value(const char * key)
    {
        return nix::valueAt(object, key);
    }

    std::string bytes(const char * key)
    {
        return nix::getString(value(key));
    }

    nix::Strings byteStrings(const char * key)
    {
        return nix::getStringList(value(key));
    }

    nix::StringPairs byteMap(const char * key)
    {
        return nix::getStringMap(value(key));
    }

    std::optional<nix::StructuredAttrs> attrs(const char * key)
    {
        if (auto value = nix::get(object, key))
            return nix::StructuredAttrs{*value};
        return std::nullopt;
    }
};

} // namespace

template<typename Inputs>
void adl_serializer<nix::derivation::Derivation<Inputs>>::to_json(
    json & res, const nix::derivation::Derivation<Inputs> & drv)
{
    res = json::object();
    res["version"] = nix::expectedJsonVersionDerivation;
    JsonWriter writer{res};
    nix::derivation::structured::write(drv, writer);
}

template<typename Inputs>
nix::derivation::Derivation<Inputs> adl_serializer<nix::derivation::Derivation<Inputs>>::from_json(
    const json & object, const nix::ExperimentalFeatureSettings & xpSettings)
{
    using namespace nix;
    auto & fields = getObject(object);
    auto version = getUnsigned(valueAt(fields, "version"));
    if (version != expectedJsonVersionDerivation)
        throw Error(
            "Unsupported derivation JSON format version %d, only format version %d is currently supported.",
            version,
            expectedJsonVersionDerivation);
    JsonReader reader{fields};
    return derivation::structured::read<Inputs>(reader, xpSettings);
}

template struct adl_serializer<nix::BasicDerivation>;
template struct adl_serializer<nix::Derivation>;

} // namespace nlohmann
