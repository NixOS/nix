#pragma once
///@file

#include <optional>
#include <string_view>
#include <utility>

namespace nix {

/**
 * Split a string once on a single-character separator.
 *
 * If `separator` is found, returns `{prefix, suffix}` where `suffix` is the
 * part after the separator. Otherwise returns `std::nullopt`.
 */
static inline std::optional<std::pair<std::string_view, std::string_view>>
splitOnce(std::string_view string, char separator)
{
    auto sepInstance = string.find(separator);

    if (sepInstance != std::string_view::npos) {
        return std::pair{
            string.substr(0, sepInstance),
            string.substr(sepInstance + 1),
        };
    }

    return std::nullopt;
}

/**
 * If `separator` is found, we return the portion of the string before the
 * separator, and modify the string argument to contain only the part after the
 * separator. Otherwise, we return `std::nullopt`, and we leave the argument
 * string alone.
 */
static inline std::optional<std::string_view> splitPrefixTo(std::string_view & string, char separator)
{
    auto sepInstance = string.find(separator);

    if (sepInstance != std::string_view::npos) {
        auto prefix = string.substr(0, sepInstance);
        string.remove_prefix(sepInstance + 1);
        return prefix;
    }

    return std::nullopt;
}

static inline bool splitPrefix(std::string_view & string, std::string_view prefix)
{
    bool res = string.starts_with(prefix);
    if (res)
        string.remove_prefix(prefix.length());
    return res;
}

} // namespace nix
