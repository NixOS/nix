#pragma once
///@file

#include "nix/util/json-impls.hh"

#include <map>

namespace nix {

/**
 * A cryptographic signature along with the name of the key that produced it.
 *
 * Serialized as `<key-name>:<signature-in-Base64>`.
 */
struct Signature
{
    std::string keyName;

    /**
     * The raw decoded signature bytes.
     */
    std::string sig;

    /**
     * Parse a signature in the format `<key-name>:<signature-in-Base64>`.
     */
    static Signature parse(std::string_view);

    /**
     * Parse multiple signatures from a container of strings.
     *
     * Each string must be in the format `<key-name>:<signature-in-Base64>`.
     */
    template<typename Container>
    static std::set<Signature> parseMany(const Container & sigStrs);

    std::string to_string() const;

    static Strings toStrings(const std::set<Signature> & sigs);

    auto operator<=>(const Signature &) const = default;
};

struct Key
{
    std::string name;
    std::string key;

    std::string to_string() const;

protected:

    /**
     * Construct Key from a string in the format
     * ‘<name>:<key-in-base64>’.
     *
     * @param sensitiveValue Avoid displaying the raw Base64 in error
     * messages to avoid leaking private keys.
     */
    Key(std::string_view s, bool sensitiveValue);

    Key(std::string_view name, std::string && key)
        : name(name)
        , key(std::move(key))
    {
    }
};

struct PublicKey;

struct SecretKey : Key
{
    SecretKey(std::string_view s);

    /**
     * Return a detached signature of the given string.
     */
    Signature signDetached(std::string_view s) const;

    PublicKey toPublicKey() const;

    static SecretKey generate(std::string_view name);

private:
    SecretKey(std::string_view name, std::string && key)
        : Key(name, std::move(key))
    {
    }
};

struct PublicKey : Key
{
    PublicKey(std::string_view data);

    /**
     * @return true iff `sig` and this key's names match, and `sig` is a
     * correct signature over `data` using the given public key.
     */
    bool verifyDetached(std::string_view data, const Signature & sig) const;

    /**
     * @return true iff `sig` is a correct signature over `data` using the
     * given public key.
     *
     * @param sig the raw signature bytes (not Base64 encoded).
     */
    bool verifyDetachedAnon(std::string_view data, const Signature & sig) const;

private:
    PublicKey(std::string_view name, std::string && key)
        : Key(name, std::move(key))
    {
    }
    friend struct SecretKey;
};

/**
 * Map from key names to public keys
 */
typedef std::map<std::string, PublicKey> PublicKeys;

/**
 * @return true iff ‘sig’ is a correct signature over ‘data’ using one
 * of the given public keys.
 */
bool verifyDetached(std::string_view data, const Signature & sig, const PublicKeys & publicKeys);

} // namespace nix

JSON_IMPL(nix::Signature)
