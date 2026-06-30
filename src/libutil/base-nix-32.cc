#include <algorithm>
#include <cassert>

#include "nix/util/base-nix-32.hh"
#include "nix/util/util.hh"

namespace nix {

constexpr const std::array<unsigned char, 256> BaseNix32::reverseMap = [] {
    std::array<unsigned char, 256> map{};

    map.fill(invalid); // invalid

    for (unsigned char i = 0; i < 32; ++i)
        map[static_cast<unsigned char>(characters[i])] = i;

    return map;
}();

std::string BaseNix32::encode(std::span<const std::byte> bs)
{
    if (bs.empty())
        return {};

    size_t len = encodedLength(bs.size());
    assert(len);

    std::string s;
    s.reserve(len);

    for (size_t n = len; n-- > 0;) {
        size_t b = n * 5;
        size_t i = b / 8;
        uint8_t j = b % 8;
        std::byte c = (bs.data()[i] >> j) | (i >= bs.size() - 1 ? std::byte{0} : bs.data()[i + 1] << (8 - j));
        s.push_back(characters[uint8_t(c & std::byte{0x1f})]);
    }

    return s;
}

std::string BaseNix32::decode(std::string_view s)
{
    if (s.empty())
        return {};

    const size_t maxSize = maxDecodedLength(s.size());
    std::string res(maxSize, '\0');
    auto * out = reinterpret_cast<uint8_t *>(res.data());
    size_t used = 0;

    for (size_t n = 0; n < s.size(); ++n) {
        char c = s[s.size() - n - 1];
        auto digit = BaseNix32::lookupReverse(c);
        if (!digit)
            throw FormatError("invalid character in Nix32 (Nix's Base32 variation) string: '%c'", c);

        size_t b = n * 5;
        size_t i = b / 8;
        uint8_t j = b % 8;

        uint8_t low = *digit << j;
        uint8_t carry = *digit >> (8 - j);

        out[i] |= low;
        used = std::max(used, i + 1);

        if (carry) {
            out[i + 1] |= carry;
            used = std::max(used, i + 2);
        }
    }

    assert(used <= maxSize);
    res.resize(used);
    return res;
}

} // namespace nix
