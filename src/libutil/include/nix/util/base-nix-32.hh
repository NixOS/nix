#pragma once
///@file

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <span>

#include "nix/util/array-from-string-literal.hh"

namespace nix {

struct BaseNix32
{
    /// omitted: E O U T
    constexpr static std::array<char, 32> characters = "0123456789abcdfghijklmnpqrsvwxyz"_arrayNoNull;

private:
    static const std::array<uint8_t, 256> reverseMap;

    const static constexpr uint8_t invalid = 0xFF;

public:
    static inline std::optional<uint8_t> lookupReverse(char base32)
    {
        uint8_t digit = reverseMap[static_cast<unsigned char>(base32)];
        if (digit == invalid)
            return std::nullopt;
        else
            return digit;
    }

    /**
     * Returns the length of a base-32 representation of this hash.
     */
    [[nodiscard]] constexpr static inline size_t encodedLength(size_t originalLength)
    {
        if (originalLength == 0)
            return 0;
        return (originalLength * 8 - 1) / 5 + 1;
    }

    /**
     * Upper bound for the number of bytes produced by decoding a base-32 string.
     */
    [[nodiscard]] constexpr static inline size_t maxDecodedLength(size_t encodedLength)
    {
        return (encodedLength * 5 + 7) / 8; // ceiling(encodedLength * 5/8)
    }

    static std::string encode(std::span<const std::byte> originalData);

    static std::string decode(std::string_view s);
};

} // namespace nix
