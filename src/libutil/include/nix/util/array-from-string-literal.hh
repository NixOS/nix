#pragma once
///@file

#include <algorithm>
#include <array>

namespace nix {

template<size_t sizeWithNull>
struct ArrayNoNullAdaptor
{
    std::array<char, sizeWithNull - 1> data;

    constexpr ArrayNoNullAdaptor(const char (&init)[sizeWithNull])
    {
        static_assert(sizeWithNull > 0);
        std::copy_n(init, sizeWithNull - 1, data.data());
    }
};

template<ArrayNoNullAdaptor str>
constexpr auto operator""_arrayNoNull()
{
    return str.data;
}

} // namespace nix
