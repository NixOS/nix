#pragma once
///@file

#include "nix/util/base-n.hh"
#include "nix/util/configuration.hh"
#include "nix/util/types.hh"
#include "nix/util/serialise.hh"
#include "nix/util/file-system.hh"
#include "nix/util/json-impls.hh"
#include "nix/util/variant-wrapper.hh"

#include <variant>

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
 * @brief Tag type for SRI (Subresource Integrity) hash format.
 *
 * SRI format is "<hash algo>-<base64 hash>".
 * @see W3C recommendation [Subresource Integrity](https://www.w3.org/TR/SRI/).
 */
struct HashFormatSRI
{
    bool operator==(const HashFormatSRI &) const = default;
    auto operator<=>(const HashFormatSRI &) const = default;
};

/**
 * @brief Hash format: either a base encoding or SRI format.
 *
 * This is a variant that can hold either:
 * - A `Base` value (Base16, Nix32, or Base64) for plain encoded hashes
 * - An `SRI` tag for Subresource Integrity format ("<algo>-<base64>")
 */
struct HashFormat
{
    using Raw = std::variant<Base, HashFormatSRI>;
    Raw raw;

    MAKE_WRAPPER_CONSTRUCTOR(HashFormat);

    bool operator==(const HashFormat &) const = default;
    auto operator<=>(const HashFormat &) const = default;

    /**
     * Get the base encoding for this hash format.
     * SRI format uses Base64.
     */
    Base toBase() const;

    /// Backwards compatibility constants
    using enum Base;
    static constexpr struct HashFormatSRI SRI{};
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
     * Like `parseAny`, but also returns the format the hash was parsed from.
     */
    static std::pair<Hash, HashFormat>
    parseAnyReturningFormat(std::string_view s, std::optional<HashAlgorithm> optAlgo);

    /**
     * Parse a plain hash that must not have any prefix indicating the type.
     * The type is passed in to disambiguate.
     */
    static Hash parseAnyPrefixed(std::string_view s);

    /**
     * Parse a plain hash that must not have any prefix indicating the type.
     * The algorithm is passed in; the base encoding is auto-detected from size.
     */
    static Hash parseNonSRIUnprefixed(std::string_view s, HashAlgorithm algo);

    /**
     * Like `parseNonSRIUnprefixed`, but the hash format has been
     * explicitly given.
     */
    static Hash parseExplicitFormatUnprefixed(
        std::string_view s,
        HashAlgorithm algo,
        Base explicitFormat,
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
 * User-friendly display of hash format (e.g., "base-64" instead of "base64").
 */
std::string_view printHashFormatDisplay(HashFormat hashFormat);

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
