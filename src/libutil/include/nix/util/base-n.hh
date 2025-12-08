#pragma once
///@file

#include <optional>
#include <string>
#include <string_view>
#include <span>

namespace nix {

/**
 * @brief Enumeration representing base-N encoding formats for binary data.
 *
 * These are pure encoding formats without any additional structure.
 * For hash-specific formats that include algorithm information (like SRI),
 * see `HashFormat`.
 */
enum struct Base : int {
    /// @brief Base 64 encoding.
    /// @see [IETF RFC 4648, section 4](https://datatracker.ietf.org/doc/html/rfc4648#section-4).
    Base64,
    /// @brief Nix-specific base-32 encoding. @see BaseNix32
    Nix32,
    /// @brief Lowercase hexadecimal encoding. @see base16Chars
    Base16,
};

/**
 * Parse a string representing a base encoding format.
 * @return std::nullopt if the string is not a valid base encoding name.
 */
std::optional<Base> parseBaseOpt(std::string_view s);

/**
 * Like parseBaseOpt but throws an error if the string is invalid.
 */
Base parseBase(std::string_view s);

/**
 * The reverse of parseBaseOpt.
 * @return A string suitable for parsing back with parseBaseOpt.
 */
std::string_view printBase(Base base);

/**
 * User-friendly display of base encoding (e.g., "base-64" instead of "base64").
 */
std::string_view printBaseDisplay(Base base);

/**
 * Given the expected size of the decoded data, figure out which base encoding
 * is being used by looking at the size of the encoded string.
 * @param encodedSize The size of the encoded string.
 * @param decodedSize The expected size of the decoded data.
 * @return std::nullopt if no base encoding matches the sizes.
 */
std::optional<Base> baseFromEncodedSize(size_t encodedSize, size_t decodedSize);

namespace base16 {

/**
 * Returns the length of a base-16 representation of this many bytes.
 */
[[nodiscard]] constexpr static inline size_t encodedLength(size_t origSize)
{
    return origSize * 2;
}

/**
 * Encode arbitrary bytes as Base16.
 */
std::string encode(std::span<const std::byte> b);

/**
 * Decode arbitrary Base16 string to bytes.
 */
std::string decode(std::string_view s);

} // namespace base16

namespace base64 {

/**
 * Returns the length of a base-64 representation of this many bytes.
 */
[[nodiscard]] constexpr static inline size_t encodedLength(size_t origSize)
{
    return ((4 * origSize / 3) + 3) & ~3;
}

/**
 * Encode arbitrary bytes as Base64.
 */
std::string encode(std::span<const std::byte> b);

/**
 * Decode arbitrary Base64 string to bytes.
 */
std::string decode(std::string_view s);

} // namespace base64

/**
 * Abstract interface for base-N encoding/decoding operations.
 */
struct BaseEncoding
{
    virtual ~BaseEncoding() = default;

    /**
     * Encode arbitrary bytes to a string.
     */
    virtual std::string encode(std::span<const std::byte> data) const = 0;

    /**
     * Decode a string to bytes.
     */
    virtual std::string decode(std::string_view s) const = 0;
};

/**
 * Get the encoding/decoding functions for the given base encoding.
 */
const BaseEncoding & getBaseEncoding(Base base);

} // namespace nix
