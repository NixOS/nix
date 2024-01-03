#pragma once
///@file

#include "types.hh"

#include <map>

namespace nix {

/**
 * Except where otherwise noted, Nix serializes keys and signatures in
 * the form:
 *
 * ```
 * <name>:<key/signature-in-Base64>
 * ```
 */
struct BorrowedCryptoValue {
    std::string_view name;
    std::string_view payload;

    /**
     * This splits on the colon, the user can then separated decode the
     * Base64 payload separately.
     */
    static BorrowedCryptoValue parse(std::string_view);
};

struct Key
{
    std::string name;
    std::string key;

    /**
     * Construct Key from a string in the format
     * ‘<name>:<key-in-base64>’.
     */
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

    /**
     * Return a detached signature of the given string.
     */
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

    /**
     * @return true iff `sig` and this key's names match, and `sig` is a
     * correct signature over `data` using the given public key.
     */
    bool verifyDetached(std::string_view data, std::string_view sigs) const;

    /**
     * @return true iff `sig` is a correct signature over `data` using the
     * given public key.
     *
     * @param just the Base64 signature itself, not a colon-separated pair of a
     * public key name and signature.
     */
    bool verifyDetachedAnon(std::string_view data, std::string_view sigs) const;

private:
    PublicKey(std::string_view name, std::string && key)
        : Key(name, std::move(key)) { }
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
bool verifyDetached(std::string_view data, std::string_view sig, const PublicKeys & publicKeys);

}
