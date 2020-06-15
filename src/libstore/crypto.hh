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
    Key(std::string_view s);

protected:
    Key(std::string_view name, std::string_view key)
        : name(name), key(key) { }
};

struct PublicKey;

struct SecretKey : Key
{
    SecretKey(std::string_view s);

    /* Return a detached signature of the given string. */
    std::string signDetached(std::string_view s) const;

    PublicKey toPublicKey() const;
};

struct PublicKey : Key
{
    PublicKey(std::string_view data);

private:
    PublicKey(std::string_view name, std::string_view key)
        : Key(name, key) { }
    friend struct SecretKey;
};

typedef std::map<std::string, PublicKey> PublicKeys;

/* Return true iff ‘sig’ is a correct signature over ‘data’ using one
   of the given public keys. */
bool verifyDetached(std::string_view data, std::string_view sig,
    const PublicKeys & publicKeys);

PublicKeys getDefaultPublicKeys();

}
