#pragma once

#include <optional>
#include <string_view>

#include "util.hh"

namespace nix {

// If `separator` is found, we return the portion of the string before the
// separator, and modify the string argument to contain only the part after the
// separator. Otherwise, we return `std::nullopt`, and we leave the argument
// string alone.
static inline std::optional<std::string_view> splitPrefixTo(std::string_view & string, char separator) {
    auto sepInstance = string.find(separator);

    if (sepInstance != std::string_view::npos) {
        auto prefix = string.substr(0, sepInstance);
        string.remove_prefix(sepInstance+1);
        return prefix;
    }

    return std::nullopt;
}

static inline bool splitPrefix(std::string_view & string, std::string_view prefix) {
    bool res = hasPrefix(string, prefix);
    if (res)
        string.remove_prefix(prefix.length());
    return res;
}

}
