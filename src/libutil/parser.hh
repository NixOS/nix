#pragma once

#include <optional>
#include <string_view>

namespace nix {

// If `separator` is found, we return the portion of the string before the
// separator, and modify the string argument to contain only the part after the
// separator. Otherwise, wer return `std::nullopt`, and we leave the argument
// string alone.
static inline std::optional<std::string_view> splitPrefix(std::string_view & string, char separator) {
    auto sepInstance = string.find(separator);

    if (sepInstance != std::string_view::npos) {
        auto prefix = string.substr(0, sepInstance);
        string.remove_prefix(sepInstance+1);
        return prefix;
    }

    return std::nullopt;
}

// Mutates the string to eliminate the prefixes when found
std::pair<std::optional<HashType>, bool> getParsedTypeAndSRI(std::string_view & rest) {
    bool isSRI = false;

    // Parse the has type before the separater, if there was one.
    std::optional<HashType> optParsedType;
    {
        auto hashRaw = splitPrefix(rest, ':');

        if (!hashRaw) {
            hashRaw = splitPrefix(rest, '-');
            if (hashRaw)
                isSRI = true;
        }
        if (hashRaw)
            optParsedType = parseHashType(*hashRaw);
    }

    return std::make_pair(optParsedType, isSRI);
}


}
