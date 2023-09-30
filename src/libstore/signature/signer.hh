#pragma once

#include "types.hh"
#include "signature/local-keys.hh"

#include <curl/curl.h>
#include <map>

namespace nix {


// An abstract signer
// Derive from this class to implement a custom signature scheme
// for the store.
//
// It is only necessary to implement signature of bytes
// and verification of data according to that signer.
class Signer
{
    public:
        virtual std::string signDetached(std::string_view s) const = 0;
        virtual bool verifyDetached(const std::string & data, const std::string & sig);
        virtual const PublicKey& getPublicKey() const;

    protected:
        Signer(PublicKey && pubkey);
        Signer();
        PublicKey pubkey;
};

typedef std::map<std::string, Signer*> Signers;

// Local signer
// The private key is held in this machine's RAM
class LocalSigner : public Signer
{
    public:
        LocalSigner(SecretKey &&privkey);

        virtual std::string signDetached(std::string_view s) const;

    private:
        SecretKey privkey;
};
}
