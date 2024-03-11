#pragma once
///@file

#include "types.hh"
#include "serialise.hh"
#include "file-system.hh"

namespace nix {


MakeError(BadHash, Error);


enum struct HashAlgorithm : char { MD5 = 42, SHA1, SHA256, SHA512 };


const int md5HashSize = 16;
const int sha1HashSize = 20;
const int sha256HashSize = 32;
const int sha512HashSize = 64;

extern const std::set<std::string> hashAlgorithms;

extern const std::string nix32Chars;

/**
 * @brief Enumeration representing the hash formats.
 */
enum struct HashFormat : int {
    /// @brief Base 64 encoding.
    /// @see [IETF RFC 4648, section 4](https://datatracker.ietf.org/doc/html/rfc4648#section-4).
    Base64,
    /// @brief Nix-specific base-32 encoding. @see nix32Chars
    Nix32,
    /// @brief Lowercase hexadecimal encoding. @see base16Chars
    Base16,
    /// @brief "<hash algo>:<Base 64 hash>", format of the SRI integrity attribute.
    /// @see W3C recommendation [Subresource Intergrity](https://www.w3.org/TR/SRI/).
    SRI
};

extern const std::set<std::string> hashFormats;

struct Hash
{
    constexpr static size_t maxHashSize = 64;
    size_t hashSize = 0;
    uint8_t hash[maxHashSize] = {};

    HashAlgorithm algo;

    /**
     * Create a zero-filled hash object.
     */
    explicit Hash(HashAlgorithm algo);

    /**
     * Parse the hash from a string representation in the format
     * "[<type>:]<base16|base32|base64>" or "<type>-<base64>" (a
     * Subresource Integrity hash expression). If the 'type' argument
     * is not present, then the hash algorithm must be specified in the
     * string.
     */
    static Hash parseAny(std::string_view s, std::optional<HashAlgorithm> optAlgo);

    /**
     * Parse a hash from a string representation like the above, except the
     * type prefix is mandatory is there is no separate arguement.
     */
    static Hash parseAnyPrefixed(std::string_view s);

    /**
     * Parse a plain hash that musst not have any prefix indicating the type.
     * The type is passed in to disambiguate.
     */
    static Hash parseNonSRIUnprefixed(std::string_view s, HashAlgorithm algo);

    static Hash parseSRI(std::string_view original);

private:
    /**
     * The type must be provided, the string view must not include <type>
     * prefix. `isSRI` helps disambigate the various base-* encodings.
     */
    Hash(std::string_view s, HashAlgorithm algo, bool isSRI);

public:
    /**
     * Check whether two hash are equal.
     */
    bool operator == (const Hash & h2) const;

    /**
     * Check whether two hash are not equal.
     */
    bool operator != (const Hash & h2) const;

    /**
     * For sorting.
     */
    bool operator < (const Hash & h) const;

    /**
     * Returns the length of a base-16 representation of this hash.
     */
    [[nodiscard]] size_t base16Len() const
    {
        return hashSize * 2;
    }

    /**
     * Returns the length of a base-32 representation of this hash.
     */
    [[nodiscard]] size_t base32Len() const
    {
        return (hashSize * 8 - 1) / 5 + 1;
    }

    /**
     * Returns the length of a base-64 representation of this hash.
     */
    [[nodiscard]] size_t base64Len() const
    {
        return ((4 * hashSize / 3) + 3) & ~3;
    }

    /**
     * Return a string representation of the hash, in base-16, base-32
     * or base-64. By default, this is prefixed by the hash algo
     * (e.g. "sha256:").
     */
    [[nodiscard]] std::string to_string(HashFormat hashFormat, bool includeAlgo) const;

    [[nodiscard]] std::string gitRev() const
    {
        return to_string(HashFormat::Base16, false);
    }

    [[nodiscard]] std::string gitShortRev() const
    {
        return std::string(to_string(HashFormat::Base16, false), 0, 7);
    }

    static Hash dummy;

    /**
     * @return a random hash with hash algorithm `algo`
     */
    static Hash random(HashAlgorithm algo);
};

/**
 * Helper that defaults empty hashes to the 0 hash.
 */
Hash newHashAllowEmpty(std::string_view hashStr, std::optional<HashAlgorithm> ha);

/**
 * Print a hash in base-16 if it's MD5, or base-32 otherwise.
 */
std::string printHash16or32(const Hash & hash);

/**
 * Compute the hash of the given string.
 */
Hash hashString(HashAlgorithm ha, std::string_view s);

/**
 * Compute the hash of the given file, hashing its contents directly.
 *
 * (Metadata, such as the executable permission bit, is ignored.)
 */
Hash hashFile(HashAlgorithm ha, const Path & path);

/**
 * The final hash and the number of bytes digested.
 *
 * @todo Convert to proper struct
 */
typedef std::pair<Hash, uint64_t> HashResult;

/**
 * Compress a hash to the specified number of bytes by cyclically
 * XORing bytes together.
 */
Hash compressHash(const Hash & hash, unsigned int newSize);

/**
 * Parse a string representing a hash format.
 */
HashFormat parseHashFormat(std::string_view hashFormatName);

/**
 * std::optional version of parseHashFormat that doesn't throw error.
 */
std::optional<HashFormat> parseHashFormatOpt(std::string_view hashFormatName);

/**
 * The reverse of parseHashFormat.
 */
std::string_view printHashFormat(HashFormat hashFormat);

/**
 * Parse a string representing a hash algorithm.
 */
HashAlgorithm parseHashAlgo(std::string_view s);

/**
 * Will return nothing on parse error
 */
std::optional<HashAlgorithm> parseHashAlgoOpt(std::string_view s);

/**
 * And the reverse.
 */
std::string_view printHashAlgo(HashAlgorithm ha);


union Ctx;

struct AbstractHashSink : virtual Sink
{
    virtual HashResult finish() = 0;
};

class HashSink : public BufferedSink, public AbstractHashSink
{
private:
    HashAlgorithm ha;
    Ctx * ctx;
    uint64_t bytes;

public:
    HashSink(HashAlgorithm ha);
    HashSink(const HashSink & h);
    ~HashSink();
    void writeUnbuffered(std::string_view data) override;
    HashResult finish() override;
    HashResult currentHash();
};


}
