#pragma once

///@file

namespace nix {

/**
 * @return true iff `c` is an ASCII letter (`A-Z` or `a-z`).
 */
constexpr bool isAsciiAlpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/**
 * @return true iff `c` is an ASCII digit (`0-9`).
 */
constexpr bool isAsciiDigit(char c)
{
    return c >= '0' && c <= '9';
}

} // namespace nix
