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

    std::string to_string() const;

protected:
    Key(std::string_view name, std::string && key)
        : name(name), key(std::move(key)) { }
};

struct PublicKey;

struct SecretKey : Key
{
    SecretKey(std::string_view s);

    /* Return a detached signature of the given string. */
    std::string signDetached(std::string_view s) const;

    PublicKey toPublicKey() const;

    static SecretKey generate(std::string_view name);

private:
    SecretKey(std::string_view name, std::string && key)
        : Key(name, std::move(key)) { }
};

struct PublicKey : Key
{
    PublicKey(std::string_view data);

private:
    PublicKey(std::string_view name, std::string && key)
        : Key(name, std::move(key)) { }
    friend struct SecretKey;
};

typedef std::map<std::string, PublicKey> PublicKeys;

/* Return true iff ‘sig’ is a correct signature over ‘data’ using one
   of the given public keys. */
bool verifyDetached(const std::string & data, const std::string & sig,
    const PublicKeys & publicKeys);

PublicKeys getDefaultPublicKeys();

}
