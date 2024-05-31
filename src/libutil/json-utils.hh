#pragma once
///@file

#include <nlohmann/json.hpp>

#include "types.hh"
#include "json-avoids-null.hh"

namespace nix {

const nlohmann::json * get(const nlohmann::json & map, const std::string & key);

nlohmann::json * get(nlohmann::json & map, const std::string & key);

/**
 * Get the value of a json object at a key safely, failing with a nice
 * error if the key does not exist.
 *
 * Use instead of nlohmann::json::at() to avoid ugly exceptions.
 */
const nlohmann::json & valueAt(
    const nlohmann::json::object_t & map,
    const std::string & key);

std::optional<nlohmann::json> optionalValueAt(const nlohmann::json::object_t & value, const std::string & key);

/**
 * Downcast the json object, failing with a nice error if the conversion fails.
 * See https://json.nlohmann.me/features/types/
 */
std::optional<nlohmann::json> getNullable(const nlohmann::json & value);
const nlohmann::json::object_t & getObject(const nlohmann::json & value);
const nlohmann::json::array_t & getArray(const nlohmann::json & value);
const nlohmann::json::string_t & getString(const nlohmann::json & value);
const nlohmann::json::number_integer_t & getInteger(const nlohmann::json & value);
const nlohmann::json::boolean_t & getBoolean(const nlohmann::json & value);
Strings getStringList(const nlohmann::json & value);
StringMap getStringMap(const nlohmann::json & value);
StringSet getStringSet(const nlohmann::json & value);

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
    /**
     * @brief Convert a JSON type to an `optional<T>` treating
     *        `null` as `std::nullopt`.
     */
    static void from_json(const json & json, std::optional<T> & t) {
        static_assert(
            nix::json_avoids_null<T>::value,
            "null is already in use for underlying type's JSON");
        t = json.is_null()
            ? std::nullopt
            : std::make_optional(json.template get<T>());
    }

    /**
     *  @brief Convert an optional type to a JSON type  treating `std::nullopt`
     *         as `null`.
     */
    static void to_json(json & json, const std::optional<T> & t) {
        static_assert(
            nix::json_avoids_null<T>::value,
            "null is already in use for underlying type's JSON");
        if (t)
            json = *t;
        else
            json = nullptr;
    }
};

}
