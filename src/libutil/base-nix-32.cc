#include <cassert>

#include "nix/util/base-nix-32.hh"

namespace nix {

constexpr const std::array<unsigned char, 256> BaseNix32::reverseMap = [] {
    std::array<unsigned char, 256> map{};

    for (size_t i = 0; i < map.size(); ++i)
        map[i] = invalid; // invalid

    for (unsigned char i = 0; i < 32; ++i)
        map[static_cast<unsigned char>(characters[i])] = i;

    return map;
}();

std::string BaseNix32::encode(std::span<const uint8_t> originalData)
{
    if (originalData.size() == 0)
        return {};

    size_t len = encodedLength(originalData.size());
    assert(len);

    std::string s;
    s.reserve(len);

    for (int n = (int) len - 1; n >= 0; n--) {
        unsigned int b = n * 5;
        unsigned int i = b / 8;
        unsigned int j = b % 8;
        unsigned char c =
            (originalData.data()[i] >> j) | (i >= originalData.size() - 1 ? 0 : originalData.data()[i + 1] << (8 - j));
        s.push_back(characters[c & 0x1f]);
    }

    return s;
}

} // namespace nix
