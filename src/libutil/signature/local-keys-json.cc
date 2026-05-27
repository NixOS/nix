#include <nlohmann/json.hpp>

#include "nix/util/base-n.hh"
#include "nix/util/signature/local-keys.hh"
#include "nix/util/json-utils.hh"

namespace nlohmann {

using namespace nix;

void adl_serializer<Signature>::to_json(json & j, const Signature & s)
{
    j = {
        {"keyName", s.keyName},
        {"sig", base64::encode(std::as_bytes(std::span<const char>{s.sig}))},
    };
}

Signature adl_serializer<Signature>::from_json(const json & j)
{
    if (j.is_string())
        return Signature::parse(getString(j));
    auto obj = getObject(j);
    return Signature{
        .keyName = getString(valueAt(obj, "keyName")),
        .sig = base64::decode(getString(valueAt(obj, "sig"))),
    };
}

void adl_serializer<PublicKey>::to_json(json & j, const PublicKey & k)
{
    j = {
        {"name", k.name},
        {"key", base64::encode(std::as_bytes(std::span<const char>{k.toSPKI()}))},
    };
}

PublicKey adl_serializer<PublicKey>::from_json(const json & j)
{
    auto obj = getObject(j);
    return PublicKey::fromSPKI(getString(valueAt(obj, "name")), base64::decode(getString(valueAt(obj, "key"))));
}

void adl_serializer<SecretKey>::to_json(json & j, const SecretKey & k)
{
    j = {
        {"name", k.name},
        {"key", base64::encode(std::as_bytes(std::span<const char>{k.toPKCS8()}))},
    };
}

SecretKey adl_serializer<SecretKey>::from_json(const json & j)
{
    auto obj = getObject(j);
    return SecretKey::fromPKCS8(getString(valueAt(obj, "name")), base64::decode(getString(valueAt(obj, "key"))));
}

} // namespace nlohmann
