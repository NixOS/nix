#pragma once
///@file

#include "nix/util/configuration.hh"
#include "nix/util/types.hh"
#include "nix/util/serialise.hh"
#include "nix/util/file-system.hh"
#include "nix/util/json-impls.hh"

namespace nix {

MakeError(BadHash, Error);

enum struct HashAlgorithm : char { MD5 = 42, SHA1, SHA256, SHA512, BLAKE3 };

/**
 * @return the size of a hash for the given algorithm
 */
constexpr inline size_t regularHashSize(HashAlgorithm type)
{
    switch (type) {
    case HashAlgorithm::BLAKE3:
        return 32;
    case HashAlgorithm::MD5:
        return 16;
    case HashAlgorithm::SHA1:
        return 20;
    case HashAlgorithm::SHA256:
        return 32;
    case HashAlgorithm::SHA512:
        return 64;
    default:
        assert(false);
    }
}

extern const StringSet hashAlgorithms;

/**
 * @brief Enumeration representing the hash formats.
 */
enum struct HashFormat : int {
    /// @brief Base 64 encoding.
    /// @see [IETF RFC 4648, section 4](https://datatracker.ietf.org/doc/html/rfc4648#section-4).
    Base64,
    /// @brief Nix-specific base-32 encoding. @see BaseNix32
    Nix32,
    /// @brief Lowercase hexadecimal encoding. @see base16Chars
    Base16,
    /// @brief "<hash algo>:<Base 64 hash>", format of the SRI integrity attribute.
    /// @see W3C recommendation [Subresource Integrity](https://www.w3.org/TR/SRI/).
    SRI
};

extern const StringSet hashFormats;

struct Hash
{
    /** Opaque handle type for the hash calculation state. */
    union Ctx;

    constexpr static size_t maxHashSize = 64;
    size_t hashSize = 0;
    uint8_t hash[maxHashSize] = {};

    HashAlgorithm algo;

    /**
     * Create a zero-filled hash object.
     */
    explicit Hash(HashAlgorithm algo, const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

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
     * type prefix is mandatory is there is no separate argument.
     */
    static Hash parseAnyPrefixed(std::string_view s);

    /**
     * Parse a plain hash that musst not have any prefix indicating the type.
     * The type is passed in to disambiguate.
     */
    static Hash parseNonSRIUnprefixed(std::string_view s, HashAlgorithm algo);

    /**
     * Like `parseNonSRIUnprefixed`, but the hash format has been
     * explicitly given.
     *
     * @param explicitFormat cannot be SRI, but must be one of the
     * "bases".
     */
    static Hash parseExplicitFormatUnprefixed(
        std::string_view s,
        HashAlgorithm algo,
        HashFormat explicitFormat,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

    static Hash parseSRI(std::string_view original);

public:
    /**
     * Check whether two hashes are equal.
     */
    bool operator==(const Hash & h2) const noexcept;

    /**
     * Compare how two hashes are ordered.
     */
    std::strong_ordering operator<=>(const Hash & h2) const noexcept;

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
 * Compute the hash of the given string.
 */
Hash hashString(
    HashAlgorithm ha, std::string_view s, const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * Compute the hash of the given file, hashing its contents directly.
 *
 * (Metadata, such as the executable permission bit, is ignored.)
 */
Hash hashFile(HashAlgorithm ha, const Path & path);

/**
 * The final hash and the number of bytes digested.
 */
struct HashResult
{
    Hash hash;
    uint64_t numBytesDigested;
};

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
HashAlgorithm
parseHashAlgo(std::string_view s, const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * Will return nothing on parse error
 */
std::optional<HashAlgorithm>
parseHashAlgoOpt(std::string_view s, const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

/**
 * And the reverse.
 */
std::string_view printHashAlgo(HashAlgorithm ha);

struct AbstractHashSink : virtual Sink
{
    virtual HashResult finish() = 0;
};

class HashSink : public BufferedSink, public AbstractHashSink
{
private:
    HashAlgorithm ha;
    Hash::Ctx * ctx;
    uint64_t bytes;

public:
    HashSink(HashAlgorithm ha);
    HashSink(const HashSink & h);
    ~HashSink();
    void writeUnbuffered(std::string_view data) override;
    HashResult finish() override;
    HashResult currentHash();
};

template<>
struct json_avoids_null<Hash> : std::true_type
{};

} // namespace nix

template<>
struct std::hash<nix::Hash>
{
    std::size_t operator()(const nix::Hash & hash) const noexcept
    {
        assert(hash.hashSize > sizeof(size_t));
        return *reinterpret_cast<const std::size_t *>(&hash.hash);
    }
};

namespace nix {

inline std::size_t hash_value(const Hash & hash)
{
    return std::hash<Hash>{}(hash);
}

} // namespace nix

JSON_IMPL_WITH_XP_FEATURES(Hash)
