#pragma once
///@file

#include <string_view>
#include <span>
#include <vector>

namespace nix {

static inline std::span<const std::byte> as_bytes(std::string_view sv) noexcept
{
    return std::span<const std::byte>{
        reinterpret_cast<const std::byte *>(sv.data()),
        sv.size(),
    };
}

static inline std::vector<std::byte> to_owned(std::span<const std::byte> bytes)
{
    return std::vector<std::byte>{
        bytes.begin(),
        bytes.end(),
    };
}

/**
 * @note this should be avoided, as arbitrary binary data in strings
 * views, while allowed, is not really proper. Generally this should
 * only be used as a stop-gap with other definitions that themselves
 * should be converted to accept `std::span<const std::byte>` or
 * similar, directly.
 */
static inline std::string_view to_str(std::span<const std::byte> sp)
{
    return std::string_view{
        reinterpret_cast<const char *>(sp.data()),
        sp.size(),
    };
}

} // namespace nix
