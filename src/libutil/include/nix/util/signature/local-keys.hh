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

struct PublicKey;

struct SecretKey
{
    std::string name;
    std::string key;

    auto operator<=>(const SecretKey &) const = default;

    /**
     * Construct from a name and raw key bytes.
     */
    SecretKey(std::string_view name, std::string && key);

    /**
     * Parse a string in the format `<name>:<key-in-Base64>`.
     */
    static SecretKey parse(std::string_view s);

    /**
     * Decode from a DER-encoded PKCS#8 / OneAsymmetricKey structure (RFC 5958, RFC 8410).
     *
     * The 32-byte seed is expanded to the full 64-byte libsodium secret key.
     */
    static SecretKey fromPKCS8(std::string_view name, std::string_view der);

    /**
     * Encode as a DER PKCS#8 / OneAsymmetricKey structure.
     *
     * Per the specification, only the 32-byte seed is stored; the public key can be rederived.
     */
    std::string toPKCS8() const;

    /**
     * Serialize as `<name>:<key-in-Base64>`.
     */
    std::string to_string() const;

    /**
     * Return a detached signature of the given string.
     */
    Signature signDetached(std::string_view s) const;

    PublicKey toPublicKey() const;

    static SecretKey generate(std::string_view name);
};

struct PublicKey
{
    std::string name;
    std::string key;

    auto operator<=>(const PublicKey &) const = default;

    /**
     * Construct from a name and raw key bytes.
     */
    PublicKey(std::string_view name, std::string && key);

    /**
     * Parse a string in the format `<name>:<key-in-Base64>`.
     */
    static PublicKey parse(std::string_view s);

    /**
     * Decode from a DER-encoded SubjectPublicKeyInfo structure (RFC 5280, RFC 8410).
     */
    static PublicKey fromSPKI(std::string_view name, std::string_view der);

    /**
     * Encode as a DER SubjectPublicKeyInfo structure.
     */
    std::string toSPKI() const;

    /**
     * Serialize as `<name>:<key-in-Base64>`.
     */
    std::string to_string() const;

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
JSON_IMPL(nix::PublicKey)
JSON_IMPL(nix::SecretKey)
