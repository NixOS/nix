#include <string_view>

#include "nix/util/array-from-string-literal.hh"
#include "nix/util/util.hh"
#include "nix/util/base-n.hh"

using namespace std::literals;

namespace nix {

constexpr static const std::array<char, 16> base16Chars = "0123456789abcdef"_arrayNoNull;

std::string base16::encode(std::span<const std::byte> b)
{
    std::string buf;
    buf.reserve(b.size() * 2);
    for (size_t i = 0; i < b.size(); i++) {
        buf.push_back(base16Chars[(uint8_t) b.data()[i] >> 4]);
        buf.push_back(base16Chars[(uint8_t) b.data()[i] & 0x0f]);
    }
    return buf;
}

std::string base16::decode(std::string_view s)
{
    auto parseHexDigit = [&](char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        throw FormatError("invalid character in Base16 string: '%c'", c);
    };

    assert(s.size() % 2 == 0);
    auto decodedSize = s.size() / 2;

    std::string res;
    res.reserve(decodedSize);

    for (unsigned int i = 0; i < decodedSize; i++) {
        res.push_back(parseHexDigit(s[i * 2]) << 4 | parseHexDigit(s[i * 2 + 1]));
    }

    return res;
}

constexpr static const std::array<char, 64> base64Chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"_arrayNoNull;

std::string base64::encode(std::span<const std::byte> s)
{
    std::string res;
    res.reserve((s.size() + 2) / 3 * 4);
    int data = 0, nbits = 0;

    for (std::byte c : s) {
        data = data << 8 | (uint8_t) c;
        nbits += 8;
        while (nbits >= 6) {
            nbits -= 6;
            res.push_back(base64Chars[data >> nbits & 0x3f]);
        }
    }

    if (nbits)
        res.push_back(base64Chars[data << (6 - nbits) & 0x3f]);
    while (res.size() % 4)
        res.push_back('=');

    return res;
}

std::string base64::decode(std::string_view s)
{
    constexpr char npos = -1;
    constexpr std::array<char, 256> base64DecodeChars = [&] {
        std::array<char, 256> result{};
        for (auto & c : result)
            c = npos;
        for (int i = 0; i < 64; i++)
            result[base64Chars[i]] = i;
        return result;
    }();

    std::string res;
    // Some sequences are missing the padding consisting of up to two '='.
    //                    vvv
    res.reserve((s.size() + 2) / 4 * 3);
    unsigned int d = 0, bits = 0;

    for (char c : s) {
        if (c == '=')
            break;
        if (c == '\n')
            continue;

        char digit = base64DecodeChars[(unsigned char) c];
        if (digit == npos)
            throw FormatError("invalid character in Base64 string: '%c'", c);

        bits += 6;
        d = d << 6 | digit;
        if (bits >= 8) {
            res.push_back(d >> (bits - 8) & 0xff);
            bits -= 8;
        }
    }

    return res;
}

} // namespace nix
