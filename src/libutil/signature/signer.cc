#include "nix/util/signature/signer.hh"
#include "nix/util/signature/local-keys.hh"

#include <sodium.h>
#include <string_view>

namespace nix {

void Signer::anchor() {}

LocalSigner::LocalSigner(SecretKey && privateKey)
    : privateKey(privateKey)
    , publicKey(privateKey.toPublicKey())
{
}

Signature LocalSigner::signDetached(std::string_view s) const
{
    return privateKey.signDetached(s);
}

const PublicKey & LocalSigner::getPublicKey()
{
    return publicKey;
}

} // namespace nix
