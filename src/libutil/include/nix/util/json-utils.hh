#pragma once
///@file

#include <nlohmann/json.hpp>

#include "nix/util/error.hh"
#include "nix/util/types.hh"
#include "nix/util/json-non-null.hh"

namespace nix {

enum struct ExperimentalFeature;

/**
 * Get the value of a json object at a key safely, failing with a nice
 * error if the key does not exist.
 *
 * Use instead of nlohmann::json::at() to avoid ugly exceptions.
 */
const nlohmann::json & valueAt(const nlohmann::json::object_t & map, std::string_view key);

/**
 * @return A pointer to the value assiocated with `key` if `value`
 * contains `key`, otherwise return  `nullptr` (not JSON `null`!).
 */
const nlohmann::json * optionalValueAt(const nlohmann::json::object_t & value, std::string_view key);

/**
 * Prevents bugs; see `get` for the same trick.
 */
const nlohmann::json & valueAt(nlohmann::json::object_t && map, std::string_view key) = delete;
const nlohmann::json * optionalValueAt(nlohmann::json::object_t && value, std::string_view key) = delete;

/**
 * Downcast the json object, failing with a nice error if the conversion fails.
 * See https://json.nlohmann.me/features/types/
 */
const nlohmann::json * getNullable(const nlohmann::json & value);
const nlohmann::json::object_t & getObject(const nlohmann::json & value);
const nlohmann::json::array_t & getArray(const nlohmann::json & value);
const nlohmann::json::string_t & getString(const nlohmann::json & value);
const nlohmann::json::number_unsigned_t & getUnsigned(const nlohmann::json & value);

template<typename T>
auto getInteger(const nlohmann::json & value) -> std::enable_if_t<std::is_signed_v<T> && std::is_integral_v<T>, T>
{
    if (auto ptr = value.get_ptr<const nlohmann::json::number_unsigned_t *>()) {
        if (*ptr <= std::make_unsigned_t<T>(std::numeric_limits<T>::max())) {
            return *ptr;
        }
    } else if (auto ptr = value.get_ptr<const nlohmann::json::number_integer_t *>()) {
        if (*ptr >= std::numeric_limits<T>::min() && *ptr <= std::numeric_limits<T>::max()) {
            return *ptr;
        }
    } else {
        auto typeName = value.is_number_float() ? "floating point number" : value.type_name();
        throw Error("Expected JSON value to be an integral number but it is of type '%s': %s", typeName, value.dump());
    }
    throw Error("Out of range: JSON value '%s' cannot be casted to %d-bit integer", value.dump(), 8 * sizeof(T));
}

template<typename... Args>
std::map<std::string, Args...> getMap(const nlohmann::json::object_t & jsonObject, auto && f)
{
    std::map<std::string, Args...> map;

    for (const auto & [key, value] : jsonObject)
        map.insert_or_assign(key, f(value));

    return map;
}

const nlohmann::json::boolean_t & getBoolean(const nlohmann::json & value);
Strings getStringList(const nlohmann::json & value);
StringMap getStringMap(const nlohmann::json & value);
StringSet getStringSet(const nlohmann::json & value);

} // namespace nix

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
struct adl_serializer<std::optional<T>>
{
    /**
     * @brief Convert a JSON type to an `optional<T>` treating
     *        `null` as `std::nullopt`.
     */
    static void from_json(const json & json, std::optional<T> & t)
    {
        static_assert(nix::json_avoids_null<T>::value, "null is already in use for underlying type's JSON");
        t = json.is_null() ? std::nullopt : std::make_optional(json.template get<T>());
    }

    /**
     *  @brief Convert an optional type to a JSON type  treating `std::nullopt`
     *         as `null`.
     */
    static void to_json(json & json, const std::optional<T> & t)
    {
        static_assert(nix::json_avoids_null<T>::value, "null is already in use for underlying type's JSON");
        if (t)
            json = *t;
        else
            json = nullptr;
    }
};

template<typename T>
static inline std::optional<T> ptrToOwned(const json * ptr)
{
    if (ptr)
        return std::optional{*ptr};
    else
        return std::nullopt;
}

} // namespace nlohmann
