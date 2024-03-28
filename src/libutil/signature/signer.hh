#pragma once

#include "types.hh"
#include "signature/local-keys.hh"

#include <curl/curl.h>
#include <map>
#include <optional>

namespace nix {

/**
 * An abstract signer
 *
 * Derive from this class to implement a custom signature scheme.
 *
 * It is only necessary to implement signature of bytes and provide a
 * public key.
 */
struct Signer
{
    virtual ~Signer() = default;

    /**
     * Sign the given data, creating a (detached) signature.
     *
     * @param data data to be signed.
     *
     * @return the [detached
     * signature](https://en.wikipedia.org/wiki/Detached_signature),
     * i.e. just the signature itself without a copy of the signed data.
     */
    virtual std::string signDetached(std::string_view data) const = 0;

    /**
     * View the public key associated with this `Signer`.
     */
    virtual const PublicKey & getPublicKey() = 0;
};

using Signers = std::map<std::string, Signer*>;

/**
 * Local signer
 *
 * The private key is held in this machine's RAM
 */
struct LocalSigner : Signer
{
    LocalSigner(SecretKey && privateKey);

    std::string signDetached(std::string_view s) const override;

    const PublicKey & getPublicKey() override;

private:

    SecretKey privateKey;
    PublicKey publicKey;
};

/**
 * Remote signer
 *
 * The remote signer adheres to the Nix Remote Signing API
 */
struct RemoteSigner : Signer
{
    RemoteSigner(const std::string & remoteServerPath);

    std::string signDetached(std::string_view s) const override;

    const PublicKey & getPublicKey() override;

private:

    std::optional<PublicKey> optPublicKey;

    std::string serverPath;
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> _curl_handle;
};

}
