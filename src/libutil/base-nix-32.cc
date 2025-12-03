#include <cassert>

#include "nix/util/base-nix-32.hh"
#include "nix/util/util.hh"

namespace nix {

constexpr const std::array<unsigned char, 256> BaseNix32::reverseMap = [] {
    std::array<unsigned char, 256> map{};

    for (size_t i = 0; i < map.size(); ++i)
        map[i] = invalid; // invalid

    for (unsigned char i = 0; i < 32; ++i)
        map[static_cast<unsigned char>(characters[i])] = i;

    return map;
}();

std::string BaseNix32::encode(std::span<const std::byte> bs)
{
    if (bs.size() == 0)
        return {};

    size_t len = encodedLength(bs.size());
    assert(len);

    std::string s;
    s.reserve(len);

    for (int n = (int) len - 1; n >= 0; n--) {
        unsigned int b = n * 5;
        unsigned int i = b / 8;
        unsigned int j = b % 8;
        std::byte c = (bs.data()[i] >> j) | (i >= bs.size() - 1 ? std::byte{0} : bs.data()[i + 1] << (8 - j));
        s.push_back(characters[uint8_t(c & std::byte{0x1f})]);
    }

    return s;
}

std::string BaseNix32::decode(std::string_view s)
{
    std::string res;
    res.reserve((s.size() * 5 + 7) / 8); // ceiling(size * 5/8)

    for (unsigned int n = 0; n < s.size(); ++n) {
        char c = s[s.size() - n - 1];
        auto digit_opt = BaseNix32::lookupReverse(c);

        if (!digit_opt)
            throw FormatError("invalid character in Nix32 (Nix's Base32 variation) string: '%c'", c);

        uint8_t digit = *digit_opt;

        unsigned int b = n * 5;
        unsigned int i = b / 8;
        unsigned int j = b % 8;

        // Ensure res has enough space
        res.resize(i + 1);
        res[i] |= digit << j;

        if (digit >> (8 - j)) {
            res.resize(i + 2);
            res[i + 1] |= digit >> (8 - j);
        }
    }

    return res;
}

} // namespace nix
