#include "signature/signer.hh"
#include "util.hh"

#include <curl/curl.h>
#include <sodium.h>

namespace nix {
    // Splits a signature, that is `$publicKey:$signatureDigest` into a pair (publicKey, signatureDigest)
    static std::pair<std::string_view, std::string_view> split(std::string_view s)
    {
        size_t colon = s.find(':');
        if (colon == std::string::npos || colon == 0)
            return {"", ""};
        return {s.substr(0, colon), s.substr(colon + 1)};
    }

    Signer::Signer() : pubkey(this->getPublicKey()) { }
    Signer::Signer(PublicKey && pubkey) : pubkey(pubkey) { }

    bool Signer::verifyDetached(const std::string & data, const std::string & sig) {
        auto ss = split(sig);

        if (std::string(ss.first) != pubkey.key) return false;

        auto sig2 = base64Decode(ss.second);
        if (sig2.size() != crypto_sign_BYTES)
            throw Error("signature is not valid");

        return crypto_sign_verify_detached((unsigned char *) sig2.data(),
            (unsigned char *) data.data(), data.size(),
            (unsigned char *) pubkey.key.data()) == 0;
    }

    const PublicKey& Signer::getPublicKey() const {
        return pubkey;
    }

    LocalSigner::LocalSigner(SecretKey && privkey) : Signer(privkey.toPublicKey()), privkey(privkey) {
    }

    std::string LocalSigner::signDetached(std::string_view s) const {
        return privkey.signDetached(s);
    }
}
