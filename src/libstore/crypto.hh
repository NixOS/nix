#pragma once

#include "types.hh"

#include <map>

namespace nix {

struct Key
{
    std::string name;
    std::string key;

    /* Construct Key from a string in the format
       ‘<name>:<key-in-base64>’. */
    Key(const std::string & s);

protected:
    Key(const std::string & name, const std::string & key)
        : name(name), key(key) { }
};

struct PublicKey;

struct SecretKey : Key
{
    SecretKey(const std::string & s);

    /* Return a detached signature of the given string. */
    std::string signDetached(const std::string & s) const;

    PublicKey toPublicKey() const;
};

struct PublicKey : Key
{
    PublicKey(const std::string & data);

private:
    PublicKey(const std::string & name, const std::string & key)
        : Key(name, key) { }
    friend struct SecretKey;
};

typedef std::map<std::string, PublicKey> PublicKeys;

/* Return true iff ‘sig’ is a correct signature over ‘data’ using one
   of the given public keys. */
bool verifyDetached(const std::string & data, const std::string & sig,
    const PublicKeys & publicKeys);

PublicKeys getDefaultPublicKeys();

}
