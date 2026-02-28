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
    unsigned int d = 0, bits = 0, char_count = 0, padding_count = 0;
    size_t padding_idx[2] = { SIZE_MAX, SIZE_MAX };
    size_t i = 0, parse_finished_at = 0;

    for (; i < s.size(); i++) {
        char c = s[i];
        if (c == '\n')
            continue;
        if (padding_count > 0 && c != '=')
            break;
        if (c == '=') {
            padding_idx[padding_count] = i;
            padding_count += 1;
            if (padding_count >= 2)
                break;
        } else {
            char digit = base64DecodeChars[(unsigned char) c];
            if (digit == npos)
                throw FormatError("invalid character in Base64 string: '%c'", c);
            char_count += 1;

            bits += 6;
            d = d << 6 | digit;
            if (bits >= 8) {
                res.push_back(d >> (bits - 8) & 0xff);
                bits -= 8;
            }
        }
    }

    unsigned int expected_padding = char_count % 3;

    if (padding_count <= expected_padding) {
        parse_finished_at = i;
    } else {
        parse_finished_at = padding_idx[expected_padding];
        assert(parse_finished_at != SIZE_MAX);
    }

    auto hash_part = s.substr(0, parse_finished_at);
    auto extraneous_part = s.substr(parse_finished_at);

    if (d != 0)
        warn("Hash '%s' has non-zero padding bits", hash_part);

    if (padding_count < expected_padding)
        warn("Hash '%s' is missing '=' padding", hash_part);

    if (parse_finished_at < s.size())
        warn("Hash '%s' has extraneous data after: '%s'", hash_part, extraneous_part);

    return res;
}

} // namespace nix
