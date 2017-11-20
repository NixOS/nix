#include "crypto.hh"
#include "util.hh"
#include "globals.hh"

#if HAVE_SODIUM
#include <sodium.h>
#endif

namespace nix {

static std::pair<std::string, std::string> split(const string & s)
{
    size_t colon = s.find(':');
    if (colon == std::string::npos || colon == 0)
        return {"", ""};
    return {std::string(s, 0, colon), std::string(s, colon + 1)};
}

Key::Key(const string & s)
{
    auto ss = split(s);

    name = ss.first;
    key = ss.second;

    if (name == "" || key == "")
        throw Error("secret key is corrupt");

    key = base64Decode(key);
}

SecretKey::SecretKey(const string & s)
    : Key(s)
{
#if HAVE_SODIUM
    if (key.size() != crypto_sign_SECRETKEYBYTES)
        throw Error("secret key is not valid");
#endif
}

#if !HAVE_SODIUM
[[noreturn]] static void noSodium()
{
    throw Error("Nix was not compiled with libsodium, required for signed binary cache support");
}
#endif

std::string SecretKey::signDetached(const std::string & data) const
{
#if HAVE_SODIUM
    unsigned char sig[crypto_sign_BYTES];
    unsigned long long sigLen;
    crypto_sign_detached(sig, &sigLen, (unsigned char *) data.data(), data.size(),
        (unsigned char *) key.data());
    return name + ":" + base64Encode(std::string((char *) sig, sigLen));
#else
    noSodium();
#endif
}

PublicKey SecretKey::toPublicKey() const
{
#if HAVE_SODIUM
    unsigned char pk[crypto_sign_PUBLICKEYBYTES];
    crypto_sign_ed25519_sk_to_pk(pk, (unsigned char *) key.data());
    return PublicKey(name, std::string((char *) pk, crypto_sign_PUBLICKEYBYTES));
#else
    noSodium();
#endif
}

PublicKey::PublicKey(const string & s)
    : Key(s)
{
#if HAVE_SODIUM
    if (key.size() != crypto_sign_PUBLICKEYBYTES)
        throw Error("public key is not valid");
#endif
}

bool verifyDetached(const std::string & data, const std::string & sig,
    const PublicKeys & publicKeys)
{
#if HAVE_SODIUM
    auto ss = split(sig);

    auto key = publicKeys.find(ss.first);
    if (key == publicKeys.end()) return false;

    auto sig2 = base64Decode(ss.second);
    if (sig2.size() != crypto_sign_BYTES)
        throw Error("signature is not valid");

    return crypto_sign_verify_detached((unsigned char *) sig2.data(),
        (unsigned char *) data.data(), data.size(),
        (unsigned char *) key->second.key.data()) == 0;
#else
    noSodium();
#endif
}

PublicKeys getDefaultPublicKeys()
{
    PublicKeys publicKeys;

    // FIXME: filter duplicates

    for (auto s : settings.trustedPublicKeys.get()) {
        PublicKey key(s);
        publicKeys.emplace(key.name, key);
    }

    for (auto secretKeyFile : settings.secretKeyFiles.get()) {
        try {
            SecretKey secretKey(readFile(secretKeyFile));
            publicKeys.emplace(secretKey.name, secretKey.toPublicKey());
        } catch (SysError & e) {
            /* Ignore unreadable key files. That's normal in a
               multi-user installation. */
        }
    }

    return publicKeys;
}

}
