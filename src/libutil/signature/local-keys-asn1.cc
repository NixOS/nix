#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/obj_mac.h>
#include <openssl/x509.h>
#include <sodium.h>

#include "nix/util/signature/local-keys.hh"
#include "nix/util/util.hh"

namespace nix {

namespace {

std::string opensslError()
{
    char buf[256];
    ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
    return buf;
}

std::string describeKeyAlgorithm(EVP_PKEY * pkey)
{
    int type = EVP_PKEY_base_id(pkey);
    const char * name = OBJ_nid2sn(type);
    return name ? name : fmt("unknown (NID %d)", type);
}

using EVP_PKEY_ptr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;

EVP_PKEY_ptr ownPkey(EVP_PKEY * raw)
{
    return EVP_PKEY_ptr{raw, EVP_PKEY_free};
}

} // anonymous namespace

PublicKey PublicKey::fromSPKI(std::string_view name, std::string_view der)
{
    auto ptr = (const unsigned char *) der.data();
    auto pkey = ownPkey(d2i_PUBKEY(nullptr, &ptr, der.size()));
    if (!pkey)
        throw FormatError("invalid SPKI encoding: %s", opensslError());

    if (EVP_PKEY_base_id(pkey.get()) != EVP_PKEY_ED25519)
        throw FormatError(
            "unsupported algorithm in SPKI: got %s, only Ed25519 is supported",
            describeKeyAlgorithm(pkey.get()));

    size_t keyLen = crypto_sign_PUBLICKEYBYTES;
    std::string keyBytes(keyLen, '\0');
    if (EVP_PKEY_get_raw_public_key(pkey.get(), (unsigned char *) keyBytes.data(), &keyLen) != 1)
        throw FormatError("failed to extract Ed25519 public key: %s", opensslError());
    keyBytes.resize(keyLen);

    return PublicKey(name, std::move(keyBytes));
}

std::string PublicKey::toSPKI() const
{
    auto pkey = ownPkey(EVP_PKEY_new_raw_public_key(
        EVP_PKEY_ED25519, nullptr, (const unsigned char *) key.data(), key.size()));
    if (!pkey)
        throw Error("failed to create Ed25519 public key: %s", opensslError());

    int len = i2d_PUBKEY(pkey.get(), nullptr);
    if (len <= 0)
        throw Error("failed to encode SPKI: %s", opensslError());

    std::string der(len, '\0');
    auto out = (unsigned char *) der.data();
    i2d_PUBKEY(pkey.get(), &out);
    return der;
}

SecretKey SecretKey::fromPKCS8(std::string_view name, std::string_view der)
{
    auto ptr = (const unsigned char *) der.data();
    auto pkey = ownPkey(d2i_AutoPrivateKey(nullptr, &ptr, der.size()));
    if (!pkey)
        throw FormatError("invalid PKCS#8 encoding: %s", opensslError());

    if (EVP_PKEY_base_id(pkey.get()) != EVP_PKEY_ED25519)
        throw FormatError(
            "unsupported algorithm in PKCS#8: got %s, only Ed25519 is supported",
            describeKeyAlgorithm(pkey.get()));

    size_t seedLen = crypto_sign_SEEDBYTES;
    unsigned char seed[crypto_sign_SEEDBYTES];
    if (EVP_PKEY_get_raw_private_key(pkey.get(), seed, &seedLen) != 1)
        throw FormatError("failed to extract Ed25519 private key: %s", opensslError());

    unsigned char pk[crypto_sign_PUBLICKEYBYTES];
    unsigned char sk[crypto_sign_SECRETKEYBYTES];
    crypto_sign_seed_keypair(pk, sk, seed);
    return SecretKey(name, std::string((char *) sk, crypto_sign_SECRETKEYBYTES));
}

std::string SecretKey::toPKCS8() const
{
    // libsodium secret key is seed(32) || public_key(32); PKCS#8 wants just the seed
    auto pkey = ownPkey(EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519, nullptr, (const unsigned char *) key.data(), crypto_sign_SEEDBYTES));
    if (!pkey)
        throw Error("failed to create Ed25519 private key: %s", opensslError());

    PKCS8_PRIV_KEY_INFO * p8 = EVP_PKEY2PKCS8(pkey.get());
    if (!p8)
        throw Error("failed to create PKCS#8 structure: %s", opensslError());

    int len = i2d_PKCS8_PRIV_KEY_INFO(p8, nullptr);
    if (len <= 0) {
        PKCS8_PRIV_KEY_INFO_free(p8);
        throw Error("failed to encode PKCS#8: %s", opensslError());
    }

    std::string der(len, '\0');
    auto out = (unsigned char *) der.data();
    i2d_PKCS8_PRIV_KEY_INFO(p8, &out);
    PKCS8_PRIV_KEY_INFO_free(p8);
    return der;
}

} // namespace nix
