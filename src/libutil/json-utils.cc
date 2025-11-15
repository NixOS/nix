#include "nix/util/json-utils.hh"
#include "nix/util/error.hh"
#include "nix/util/types.hh"
#include "nix/util/util.hh"

namespace nix {

const nlohmann::json & valueAt(const nlohmann::json::object_t & map, std::string_view key)
{
    if (auto * p = optionalValueAt(map, key))
        return *p;
    else
        throw Error("Expected JSON object to contain key '%s' but it doesn't: %s", key, nlohmann::json(map).dump());
}

const nlohmann::json * optionalValueAt(const nlohmann::json::object_t & map, std::string_view key)
{
    return get(map, key);
}

const nlohmann::json * getNullable(const nlohmann::json & value)
{
    return value.is_null() ? nullptr : &value;
}

/**
 * Ensure the type of a JSON object is what you expect, failing with a
 * ensure type if it isn't.
 *
 * Use before type conversions and element access to avoid ugly
 * exceptions, but only part of this module to define the other `get*`
 * functions. It is too cumbersome and easy to forget to expect regular
 * JSON code to use it directly.
 */
static const nlohmann::json & ensureType(const nlohmann::json & value, nlohmann::json::value_type expectedType)
{
    if (value.type() != expectedType)
        throw Error(
            "Expected JSON value to be of type '%s' but it is of type '%s': %s",
            nlohmann::json(expectedType).type_name(),
            value.type_name(),
            value.dump());

    return value;
}

const nlohmann::json::object_t & getObject(const nlohmann::json & value)
{
    return ensureType(value, nlohmann::json::value_t::object).get_ref<const nlohmann::json::object_t &>();
}

const nlohmann::json::array_t & getArray(const nlohmann::json & value)
{
    return ensureType(value, nlohmann::json::value_t::array).get_ref<const nlohmann::json::array_t &>();
}

const nlohmann::json::string_t & getString(const nlohmann::json & value)
{
    return ensureType(value, nlohmann::json::value_t::string).get_ref<const nlohmann::json::string_t &>();
}

const nlohmann::json::number_unsigned_t & getUnsigned(const nlohmann::json & value)
{
    if (auto ptr = value.get<const nlohmann::json::number_unsigned_t *>()) {
        return *ptr;
    }
    const char * typeName = value.type_name();
    if (typeName == nlohmann::json(0).type_name()) {
        typeName = value.is_number_float() ? "floating point number" : "signed integral number";
    }
    throw Error(
        "Expected JSON value to be an unsigned integral number but it is of type '%s': %s", typeName, value.dump());
}

const nlohmann::json::boolean_t & getBoolean(const nlohmann::json & value)
{
    return ensureType(value, nlohmann::json::value_t::boolean).get_ref<const nlohmann::json::boolean_t &>();
}

Strings getStringList(const nlohmann::json & value)
{
    auto & jsonArray = getArray(value);

    Strings stringList;

    for (const auto & elem : jsonArray)
        stringList.push_back(getString(elem));

    return stringList;
}

StringMap getStringMap(const nlohmann::json & value)
{
    return getMap<std::string, std::less<>>(getObject(value), getString);
}

StringSet getStringSet(const nlohmann::json & value)
{
    auto & jsonArray = getArray(value);

    StringSet stringSet;

    for (const auto & elem : jsonArray)
        stringSet.insert(getString(elem));

    return stringSet;
}
} // namespace nix
