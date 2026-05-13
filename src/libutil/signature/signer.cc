#include "nix/util/signature/signer.hh"
#include "nix/util/error.hh"

#include <sodium.h>

namespace nix {

LocalSigner::LocalSigner(std::unique_ptr<SecretKey> && _privateKey)
    : privateKey(std::move(_privateKey))
    , publicKey(privateKey->toPublicKey())
{
}

Signature LocalSigner::signDetached(std::string_view s) const
{
    return privateKey->signDetached(s);
}

const PublicKey & LocalSigner::getPublicKey()
{
    return publicKey;
}

} // namespace nix
