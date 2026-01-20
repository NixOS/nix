#include <nlohmann/json.hpp>
#include <ranges>
#include <sodium.h>

#include "nix/util/base-n.hh"
#include "nix/util/signature/local-keys.hh"
#include "nix/util/json-utils.hh"
#include "nix/util/util.hh"

namespace nix {

namespace {

/**
 * Parse a colon-separated string where the second part is Base64-encoded.
 *
 * @param s The string to parse in the format `<name>:<base64-data>`.
 * @param typeName Name of the type being parsed (for error messages).
 * @return A pair of (name, decoded-data).
 */
std::pair<std::string, std::string> parseColonBase64(std::string_view s, std::string_view typeName)
{
    size_t colon = s.find(':');
    if (colon == std::string::npos || colon == 0)
        throw FormatError("%s is corrupt", typeName);

    auto name = std::string(s.substr(0, colon));
    auto data = base64::decode(s.substr(colon + 1));

    if (name.empty() || data.empty())
        throw FormatError("%s is corrupt", typeName);

    return {std::move(name), std::move(data)};
}

/**
 * Serialize a name and data to a colon-separated string with Base64 encoding.
 *
 * @param name The name part.
 * @param data The raw data to be Base64-encoded.
 * @return A string in the format `<name>:<base64-data>`.
 */
std::string serializeColonBase64(std::string_view name, std::string_view data)
{
    return std::string(name) + ":" + base64::encode(std::as_bytes(std::span<const char>{data.data(), data.size()}));
}

} // anonymous namespace

Signature Signature::parse(std::string_view s)
{
    auto [keyName, sig] = parseColonBase64(s, "signature");
    return Signature{
        .keyName = std::move(keyName),
        .sig = std::move(sig),
    };
}

std::string Signature::to_string() const
{
    return serializeColonBase64(keyName, sig);
}

template<typename Container>
std::set<Signature> Signature::parseMany(const Container & sigStrs)
{
    auto parsed = sigStrs | std::views::transform([](const auto & s) { return Signature::parse(s); });
    return std::set<Signature>(parsed.begin(), parsed.end());
}

template std::set<Signature> Signature::parseMany(const Strings &);
template std::set<Signature> Signature::parseMany(const StringSet &);

Strings Signature::toStrings(const std::set<Signature> & sigs)
{
    Strings res;
    for (const auto & sig : sigs) {
        res.push_back(sig.to_string());
    }

    return res;
}

Key::Key(std::string_view s, bool sensitiveValue)
{
    try {
        auto [parsedName, parsedKey] = parseColonBase64(s, "key");
        name = std::move(parsedName);
        key = std::move(parsedKey);
    } catch (Error & e) {
        std::string extra;
        if (!sensitiveValue)
            extra = fmt(" with raw value '%s'", s);
        e.addTrace({}, "while decoding key named '%s'%s", name, extra);
        throw;
    }
}

std::string Key::to_string() const
{
    return serializeColonBase64(name, key);
}

SecretKey::SecretKey(std::string_view s)
    : Key{s, true}
{
    if (key.size() != crypto_sign_SECRETKEYBYTES)
        throw Error("secret key is not valid");
}

Signature SecretKey::signDetached(std::string_view data) const
{
    unsigned char sig[crypto_sign_BYTES];
    unsigned long long sigLen;
    crypto_sign_detached(sig, &sigLen, (unsigned char *) data.data(), data.size(), (unsigned char *) key.data());
    return Signature{
        .keyName = name,
        .sig = std::string((char *) sig, sigLen),
    };
}

PublicKey SecretKey::toPublicKey() const
{
    unsigned char pk[crypto_sign_PUBLICKEYBYTES];
    crypto_sign_ed25519_sk_to_pk(pk, (unsigned char *) key.data());
    return PublicKey(name, std::string((char *) pk, crypto_sign_PUBLICKEYBYTES));
}

SecretKey SecretKey::generate(std::string_view name)
{
    unsigned char pk[crypto_sign_PUBLICKEYBYTES];
    unsigned char sk[crypto_sign_SECRETKEYBYTES];
    if (crypto_sign_keypair(pk, sk) != 0)
        throw Error("key generation failed");

    return SecretKey(name, std::string((char *) sk, crypto_sign_SECRETKEYBYTES));
}

PublicKey::PublicKey(std::string_view s)
    : Key{s, false}
{
    if (key.size() != crypto_sign_PUBLICKEYBYTES)
        throw Error("public key is not valid");
}

bool PublicKey::verifyDetached(std::string_view data, const Signature & sig) const
{
    if (sig.keyName != name)
        return false;

    return verifyDetachedAnon(data, sig);
}

bool PublicKey::verifyDetachedAnon(std::string_view data, const Signature & sig) const
{
    if (sig.sig.size() != crypto_sign_BYTES)
        throw Error("signature is not valid");

    return crypto_sign_verify_detached(
               (unsigned char *) sig.sig.data(),
               (unsigned char *) data.data(),
               data.size(),
               (unsigned char *) key.data())
           == 0;
}

bool verifyDetached(std::string_view data, const Signature & sig, const PublicKeys & publicKeys)
{
    auto key = publicKeys.find(sig.keyName);
    if (key == publicKeys.end())
        return false;

    return key->second.verifyDetachedAnon(data, sig);
}

} // namespace nix

namespace nlohmann {
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

} // namespace nlohmann
