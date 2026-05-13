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

enum KeyType {
    Ed25519,
    MLDSA44,
    MLDSA65,
    MLDSA87,
};

KeyType parseKeyType(std::string_view s);

struct Key
{
    const std::string name;
    const std::string key;

    std::string to_string() const;

protected:

    Key(std::string_view name, std::string && key)
        : name(name)
        , key(std::move(key))
    {
    }
};

struct PublicKey;

struct SecretKey : Key
{
    using Key::Key;

    virtual ~SecretKey() {};

    static std::unique_ptr<SecretKey> parse(std::string_view s);

    /**
     * Return a detached signature of the given string.
     */
    virtual Signature signDetached(std::string_view s) const;

    virtual std::unique_ptr<PublicKey> toPublicKey() const;

    /**
     * Return a PEM PKCS#8 encoding of this secret key. The Nix-specific
     * key name is not included. Only ML-DSA keys are supported.
     */
    virtual std::string toPEM() const;

    static std::unique_ptr<SecretKey> generate(std::string_view name, KeyType type);
};

struct PublicKey : Key
{
    using Key::Key;

    virtual ~PublicKey() {};

    static std::unique_ptr<PublicKey> parse(std::string_view s);

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
    virtual bool verifyDetachedAnon(std::string_view data, const Signature & sig) const;

    /**
     * Return a PEM SubjectPublicKeyInfo encoding of this public key.
     * The Nix-specific key name is not included. Only ML-DSA keys are
     * supported.
     */
    virtual std::string toPEM() const;
};

/**
 * Map from key names to public keys
 */
typedef std::map<std::string, std::unique_ptr<PublicKey>> PublicKeys;

/**
 * @return true iff ‘sig’ is a correct signature over ‘data’ using one
 * of the given public keys.
 */
bool verifyDetached(std::string_view data, const Signature & sig, const PublicKeys & publicKeys);

} // namespace nix

JSON_IMPL(nix::Signature)
