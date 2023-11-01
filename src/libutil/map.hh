#pragma once

#include <map>
#include <optional>

namespace nix {

    /**
     * Return the value associated with the key, or `std::nullopt` if the key
     * is not present.
     */
    template <typename K, typename V>
    std::optional<V> maybeGet(const std::map<K, V> & m, const K & k) {
        auto i = m.find(k);
        if (i == m.end()) return std::nullopt;
        return std::make_optional(i->second);
    }

}
