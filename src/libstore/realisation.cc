#include "nix/store/realisation.hh"
#include "nix/store/store-api.hh"
#include "nix/util/signature/local-keys.hh"
#include "nix/util/json-utils.hh"
#include <nlohmann/json.hpp>

namespace nix {

MakeError(InvalidDerivationOutputId, Error);

DrvOutput DrvOutput::parse(const std::string & strRep)
{
    size_t n = strRep.find("!");
    if (n == strRep.npos)
        throw InvalidDerivationOutputId("Invalid derivation output id %s", strRep);

    return DrvOutput{
        .drvHash = Hash::parseAnyPrefixed(strRep.substr(0, n)),
        .outputName = strRep.substr(n + 1),
    };
}

std::string DrvOutput::to_string() const
{
    return strHash() + "!" + outputName;
}

std::string UnkeyedRealisation::fingerprint(const DrvOutput & key) const
{
    nlohmann::json serialized = Realisation{*this, key};
    serialized.erase("signatures");
    return serialized.dump();
}

void UnkeyedRealisation::sign(const DrvOutput & key, const Signer & signer)
{
    signatures.insert(signer.signDetached(fingerprint(key)));
}

bool UnkeyedRealisation::checkSignature(
    const DrvOutput & key, const PublicKeys & publicKeys, const std::string & sig) const
{
    return verifyDetached(fingerprint(key), sig, publicKeys);
}

size_t UnkeyedRealisation::checkSignatures(const DrvOutput & key, const PublicKeys & publicKeys) const
{
    // FIXME: Maybe we should return `maxSigs` if the realisation corresponds to
    // an input-addressed one − because in that case the drv is enough to check
    // it − but we can't know that here.

    size_t good = 0;
    for (auto & sig : signatures)
        if (checkSignature(key, publicKeys, sig))
            good++;
    return good;
}

const StorePath & RealisedPath::path() const &
{
    return std::visit([](auto & arg) -> auto & { return arg.getPath(); }, raw);
}

bool Realisation::isCompatibleWith(const UnkeyedRealisation & other) const
{
    return outPath == other.outPath;
}

} // namespace nix

namespace nlohmann {

using namespace nix;

UnkeyedRealisation adl_serializer<UnkeyedRealisation>::from_json(const json & json0)
{
    auto json = getObject(json0);

    StringSet signatures;
    if (auto signaturesOpt = optionalValueAt(json, "signatures"))
        signatures = *signaturesOpt;

    return UnkeyedRealisation{
        .outPath = valueAt(json, "outPath"),
        .signatures = signatures,
    };
}

void adl_serializer<UnkeyedRealisation>::to_json(json & json, const UnkeyedRealisation & r)
{
    json = {
        {"outPath", r.outPath},
        {"signatures", r.signatures},
        // back-compat
        {"dependentRealisations", json::object()},
    };
}

Realisation adl_serializer<Realisation>::from_json(const json & json0)
{
    auto json = getObject(json0);

    return Realisation{
        static_cast<UnkeyedRealisation>(json0),
        DrvOutput::parse(valueAt(json, "id")),
    };
}

void adl_serializer<Realisation>::to_json(json & json, const Realisation & r)
{
    json = static_cast<const UnkeyedRealisation &>(r);
    json["id"] = r.id.to_string();
}

} // namespace nlohmann
