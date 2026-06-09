#pragma once
///@file

#include "nix/util/error.hh"

#include <cassert>
#include <limits>
#include <type_traits>
#include <bit>

namespace nix {

/// Aligns val upwards to be a multiple of alignment.
///
/// @pre alignment must be a power of 2.
template<typename T>
    requires std::is_unsigned_v<T>
constexpr T alignUp(T val, unsigned alignment)
{
    assert(std::has_single_bit(alignment) && "alignment must be a power of 2");
    assert(alignment <= std::numeric_limits<T>::max());
    T mask = ~(static_cast<T>(alignment) - 1u);
    if (val > std::numeric_limits<T>::max() - (alignment - 1)) /* Overflow check. */
        throw Error("can't align %d to %d: value is too large", val, alignment);
    return (val + alignment - 1) & mask;
}

} // namespace nix
