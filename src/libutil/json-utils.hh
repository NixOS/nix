#pragma once
///@file

#include <nlohmann/json.hpp>
#include <list>

namespace nix {

const nlohmann::json * get(const nlohmann::json & map, const std::string & key);

nlohmann::json * get(nlohmann::json & map, const std::string & key);

/**
 * For `adl_serializer<std::optional<T>>` below, we need to track what
 * types are not already using `null`. Only for them can we use `null`
 * to represent `std::nullopt`.
 */
template<typename T>
struct json_avoids_null;

/**
 * Handle numbers in default impl
 */
template<typename T>
struct json_avoids_null : std::bool_constant<std::is_integral<T>::value> {};

template<>
struct json_avoids_null<std::nullptr_t> : std::false_type {};

template<>
struct json_avoids_null<bool> : std::true_type {};

template<>
struct json_avoids_null<std::string> : std::true_type {};

template<typename T>
struct json_avoids_null<std::vector<T>> : std::true_type {};

template<typename T>
struct json_avoids_null<std::list<T>> : std::true_type {};

template<typename K, typename V>
struct json_avoids_null<std::map<K, V>> : std::true_type {};

}

namespace nlohmann {

/**
 * This "instance" is widely requested, see
 * https://github.com/nlohmann/json/issues/1749, but momentum has stalled
 * out. Writing there here in Nix as a stop-gap.
 *
 * We need to make sure the underlying type does not use `null` for this to
 * round trip. We do that with a static assert.
 */
template<typename T>
struct adl_serializer<std::optional<T>> {
    static std::optional<T> from_json(const json & json) {
        static_assert(
            nix::json_avoids_null<T>::value,
            "null is already in use for underlying type's JSON");
        return json.is_null()
            ? std::nullopt
            : std::optional { adl_serializer<T>::from_json(json) };
    }
    static void to_json(json & json, std::optional<T> t) {
        static_assert(
            nix::json_avoids_null<T>::value,
            "null is already in use for underlying type's JSON");
        if (t)
            adl_serializer<T>::to_json(json, *t);
        else
            json = nullptr;
    }
};

}
