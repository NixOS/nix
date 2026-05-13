#include <nlohmann/json.hpp>
#include <ranges>
#include <sodium.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include "nix/util/base-n.hh"
#include "nix/util/signature/local-keys.hh"
#include "nix/util/json-utils.hh"
#include "nix/util/util.hh"
#include "nix/util/deleter.hh"

namespace nix {

namespace {

using AutoEVP_PKEY = std::unique_ptr<EVP_PKEY, Deleter<EVP_PKEY_free>>;
using AutoEVP_PKEY_CTX = std::unique_ptr<EVP_PKEY_CTX, Deleter<EVP_PKEY_CTX_free>>;
using AutoEVP_MD_CTX = std::unique_ptr<EVP_MD_CTX, Deleter<EVP_MD_CTX_free>>;

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

static const char * toOpenSSLKeyType(KeyType type)
{
    switch (type) {
    case KeyType::MLDSA44:
        return "ML-DSA-44";
    case KeyType::MLDSA65:
        return "ML-DSA-65";
    case KeyType::MLDSA87:
        return "ML-DSA-87";
    case KeyType::Ed25519:
        throw Error("key type is not supported by OpenSSL");
    }
}

/**
 * Parse a DER-encoded PKCS#8 `PrivateKeyInfo` and verify that the key is ML-DSA-*.
 */
static AutoEVP_PKEY parsePrivateKey(std::string_view der)
{
    auto p = (const unsigned char *) der.data();
    AutoEVP_PKEY pkey(d2i_AutoPrivateKey(nullptr, &p, der.size()));
    return pkey;
}

static AutoEVP_PKEY parsePrivateKey(std::string_view der, KeyType type)
{
    auto pkey = parsePrivateKey(der);
    if (!pkey)
        throw Error("private key is not PKCS#8-encoded");

    auto typeS = toOpenSSLKeyType(type);

    if (EVP_PKEY_is_a(pkey.get(), typeS) != 1)
        throw Error("private key is not '%s' (got '%s')", typeS, EVP_PKEY_get0_type_name(pkey.get()));

    return pkey;
}

/**
 * Parse a DER-encoded `SubjectPublicKeyInfo` and verify that the key is ML-DSA-*.
 */
static AutoEVP_PKEY parsePublicKey(std::string_view der)
{
    auto p = (const unsigned char *) der.data();
    AutoEVP_PKEY pkey(d2i_PUBKEY(nullptr, &p, der.size()));
    return pkey;
}

static AutoEVP_PKEY parsePublicKey(std::string_view der, KeyType type)
{
    auto pkey = parsePublicKey(der);

    auto typeS = toOpenSSLKeyType(type);

    if (EVP_PKEY_is_a(pkey.get(), typeS) != 1)
        throw Error("public key is not '%s'' (got '%s')", typeS, EVP_PKEY_get0_type_name(pkey.get()));

    return pkey;
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

KeyType parseKeyType(std::string_view s)
{
    static std::unordered_map<std::string_view, KeyType> keyTypeMap{
        {"ed25519", KeyType::Ed25519},
        {"ml-dsa-44", KeyType::MLDSA44},
        {"ml-dsa-65", KeyType::MLDSA65},
        {"ml-dsa-87", KeyType::MLDSA87},
    };
    auto i = keyTypeMap.find(s);
    if (i != keyTypeMap.end())
        return i->second;
    static StringSet validKeyTypes = [] {
        StringSet s;
        for (const auto & [k, _] : keyTypeMap) {
            s.insert(std::string(k));
        }
        return s;
    }();
    throw UsageError("unknown key type '%s'; valid key types are %s", s, concatStringsSep(", ", validKeyTypes));
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
    if (key.size() == crypto_sign_SECRETKEYBYTES)
        type = KeyType::Ed25519;
    else if (auto pkey = parsePrivateKey(key)) {
        if (EVP_PKEY_is_a(pkey.get(), "ML-DSA-44") == 1)
            type = KeyType::MLDSA44;
        else if (EVP_PKEY_is_a(pkey.get(), "ML-DSA-65") == 1)
            type = KeyType::MLDSA65;
        else if (EVP_PKEY_is_a(pkey.get(), "ML-DSA-87") == 1)
            type = KeyType::MLDSA87;
        else
            throw Error("secret key has unsupported type '%s'", EVP_PKEY_get0_type_name(pkey.get()));
    } else
        throw Error("secret key is not valid");
}

Signature SecretKey::signDetached(std::string_view data) const
{
    switch (type) {

    case KeyType::Ed25519:
        unsigned char sig[crypto_sign_BYTES];
        unsigned long long sigLen;
        crypto_sign_detached(sig, &sigLen, (unsigned char *) data.data(), data.size(), (unsigned char *) key.data());
        return Signature{
            .keyName = name,
            .sig = std::string((char *) sig, sigLen),
        };

    case KeyType::MLDSA44:
    case KeyType::MLDSA65:
    case KeyType::MLDSA87: {
        auto pkey = parsePrivateKey(key, type);

        AutoEVP_MD_CTX ctx(EVP_MD_CTX_new());
        if (!ctx)
            throw Error("EVP_MD_CTX_new failed");

        /* Generate a deterministic signature (i.e. only depending on the key and the data) since Ed25519 is also
           deterministic. Note from RFC-9882: "The signer SHOULD NOT use the deterministic variant of ML-DSA on
           platforms where side-channel attacks or fault attacks are a concern." */
        int deterministic = 1;
        OSSL_PARAM params[] = {
            OSSL_PARAM_construct_int(OSSL_SIGNATURE_PARAM_DETERMINISTIC, &deterministic),
            OSSL_PARAM_construct_end(),
        };

        if (EVP_DigestSignInit_ex(ctx.get(), nullptr, nullptr, nullptr, nullptr, pkey.get(), params) <= 0)
            throw Error("EVP_DigestSignInit_ex failed");

        size_t sigLen = 0;
        if (EVP_DigestSign(ctx.get(), nullptr, &sigLen, (const unsigned char *) data.data(), data.size()) <= 0)
            throw Error("EVP_DigestSign (get length) failed");

        std::string sig(sigLen, '\0');
        if (EVP_DigestSign(
                ctx.get(), (unsigned char *) sig.data(), &sigLen, (const unsigned char *) data.data(), data.size())
            <= 0)
            throw Error("EVP_DigestSign failed");
        sig.resize(sigLen);

        return Signature{
            .keyName = name,
            .sig = std::move(sig),
        };
    }

    default:
        unreachable();
    }
}

PublicKey SecretKey::toPublicKey() const
{
    switch (type) {

    case KeyType::Ed25519:
        unsigned char pk[crypto_sign_PUBLICKEYBYTES];
        crypto_sign_ed25519_sk_to_pk(pk, (unsigned char *) key.data());
        return PublicKey(type, name, std::string((char *) pk, crypto_sign_PUBLICKEYBYTES));

    case KeyType::MLDSA44:
    case KeyType::MLDSA65:
    case KeyType::MLDSA87: {
        auto pkey = parsePrivateKey(key, type);

        unsigned char * derBuf = nullptr;
        int derLen = i2d_PUBKEY(pkey.get(), &derBuf);
        if (derLen < 0)
            throw Error("i2d_PUBKEY failed");
        std::string der((const char *) derBuf, derLen);
        OPENSSL_free(derBuf);

        return PublicKey(type, name, std::move(der));
    }

    default:
        unreachable();
    }
}

SecretKey SecretKey::generate(std::string_view name, KeyType type)
{
    switch (type) {

    case KeyType::Ed25519:
        unsigned char pk[crypto_sign_PUBLICKEYBYTES];
        unsigned char sk[crypto_sign_SECRETKEYBYTES];
        if (crypto_sign_keypair(pk, sk) != 0)
            throw Error("key generation failed");

        return SecretKey(KeyType::Ed25519, name, std::string((char *) sk, crypto_sign_SECRETKEYBYTES));

    case KeyType::MLDSA44:
    case KeyType::MLDSA65:
    case KeyType::MLDSA87: {
        auto typeS = toOpenSSLKeyType(type);
        AutoEVP_PKEY_CTX ctx(EVP_PKEY_CTX_new_from_name(nullptr, typeS, nullptr));
        if (!ctx)
            throw Error("EVP_PKEY_CTX_new_from_name failed for '%s'", typeS);

        if (EVP_PKEY_keygen_init(ctx.get()) <= 0)
            throw Error("EVP_PKEY_keygen_init failed");

        EVP_PKEY * rawPkey = nullptr;
        if (EVP_PKEY_generate(ctx.get(), &rawPkey) <= 0)
            throw Error("EVP_PKEY_generate failed");
        AutoEVP_PKEY pkey(rawPkey);

        unsigned char * derBuf = nullptr;
        int derLen = i2d_PrivateKey(pkey.get(), &derBuf);
        if (derLen < 0)
            throw Error("i2d_PrivateKey failed");
        std::string der((const char *) derBuf, derLen);
        OPENSSL_free(derBuf);

        return SecretKey(type, name, std::move(der));
    }

    default:
        unreachable();
    }
}

PublicKey::PublicKey(std::string_view s)
    : Key{s, false}
{
    if (key.size() == crypto_sign_PUBLICKEYBYTES)
        type = KeyType::Ed25519;
    else if (auto pkey = parsePublicKey(key)) {
        if (EVP_PKEY_is_a(pkey.get(), "ML-DSA-44") == 1)
            type = KeyType::MLDSA44;
        else if (EVP_PKEY_is_a(pkey.get(), "ML-DSA-65") == 1)
            type = KeyType::MLDSA65;
        else if (EVP_PKEY_is_a(pkey.get(), "ML-DSA-87") == 1)
            type = KeyType::MLDSA87;
        else
            throw Error("public key has unsupported type '%s'", EVP_PKEY_get0_type_name(pkey.get()));
    } else
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
    switch (type) {

    case KeyType::Ed25519:
        if (sig.sig.size() != crypto_sign_BYTES)
            return false;

        return crypto_sign_verify_detached(
                   (unsigned char *) sig.sig.data(),
                   (unsigned char *) data.data(),
                   data.size(),
                   (unsigned char *) key.data())
               == 0;

    case KeyType::MLDSA44:
    case KeyType::MLDSA65:
    case KeyType::MLDSA87: {
        auto pkey = parsePublicKey(key, type);

        AutoEVP_MD_CTX ctx(EVP_MD_CTX_new());
        if (!ctx)
            throw Error("EVP_MD_CTX_new failed");

        if (EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, pkey.get()) <= 0)
            throw Error("EVP_DigestVerifyInit failed");

        return EVP_DigestVerify(
                   ctx.get(),
                   (const unsigned char *) sig.sig.data(),
                   sig.sig.size(),
                   (const unsigned char *) data.data(),
                   data.size())
               == 1;
    }

    default:
        unreachable();
    }
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
    j = s.to_string();
}

Signature adl_serializer<Signature>::from_json(const json & j)
{
    return Signature::parse(getString(j));
}

} // namespace nlohmann
