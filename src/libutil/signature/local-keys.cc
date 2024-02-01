#include "signature/local-keys.hh"

#include "file-system.hh"
#include "util.hh"
#include <sodium.h>

namespace nix {

BorrowedCryptoValue BorrowedCryptoValue::parse(std::string_view s)
{
    size_t colon = s.find(':');
    if (colon == std::string::npos || colon == 0)
        return {"", ""};
    return {s.substr(0, colon), s.substr(colon + 1)};
}

Key::Key(std::string_view s)
{
    auto ss = BorrowedCryptoValue::parse(s);

    name = ss.name;
    key = ss.payload;

    if (name == "" || key == "")
        throw Error("secret key is corrupt");

    key = base64Decode(key);
}

std::string Key::to_string() const
{
    return name + ":" + base64Encode(key);
}

SecretKey::SecretKey(std::string_view s)
    : Key(s)
{
    if (key.size() != crypto_sign_SECRETKEYBYTES)
        throw Error("secret key is not valid");
}

std::string SecretKey::signDetached(std::string_view data) const
{
    unsigned char sig[crypto_sign_BYTES];
    unsigned long long sigLen;
    crypto_sign_detached(sig, &sigLen, (unsigned char *) data.data(), data.size(),
        (unsigned char *) key.data());
    return name + ":" + base64Encode(std::string((char *) sig, sigLen));
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
    : Key(s)
{
    if (key.size() != crypto_sign_PUBLICKEYBYTES)
        throw Error("public key is not valid");
}

bool PublicKey::verifyDetached(std::string_view data, std::string_view sig) const
{
    auto ss = BorrowedCryptoValue::parse(sig);

    if (ss.name != std::string_view { name }) return false;

    return verifyDetachedAnon(data, ss.payload);
}

bool PublicKey::verifyDetachedAnon(std::string_view data, std::string_view sig) const
{
    auto sig2 = base64Decode(sig);
    if (sig2.size() != crypto_sign_BYTES)
        throw Error("signature is not valid");

    return crypto_sign_verify_detached((unsigned char *) sig2.data(),
        (unsigned char *) data.data(), data.size(),
        (unsigned char *) key.data()) == 0;
}

bool verifyDetached(std::string_view data, std::string_view sig, const PublicKeys & publicKeys)
{
    auto ss = BorrowedCryptoValue::parse(sig);

    auto key = publicKeys.find(std::string(ss.name));
    if (key == publicKeys.end()) return false;

    return key->second.verifyDetachedAnon(data, ss.payload);
}

}
