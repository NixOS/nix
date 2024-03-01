#include "json-utils.hh"
#include "error.hh"
#include "types.hh"

namespace nix {

const nlohmann::json * get(const nlohmann::json & map, const std::string & key)
{
    auto i = map.find(key);
    if (i == map.end()) return nullptr;
    return &*i;
}

nlohmann::json * get(nlohmann::json & map, const std::string & key)
{
    auto i = map.find(key);
    if (i == map.end()) return nullptr;
    return &*i;
}

const nlohmann::json & valueAt(
    const nlohmann::json & map,
    const std::string & key)
{
    if (!map.contains(key))
        throw Error("Expected JSON object to contain key '%s' but it doesn't", key);

    return map[key];
}

const nlohmann::json & ensureType(
    const nlohmann::json & value,
    nlohmann::json::value_type expectedType
    )
{
    if (value.type() != expectedType)
        throw Error(
            "Expected JSON value to be of type '%s' but it is of type '%s'",
            nlohmann::json(expectedType).type_name(),
            value.type_name());

    return value;
}

const nlohmann::json::object_t & getObject(const nlohmann::json & value)
{
    if (!value.is_array())
        throw Error(
            "Expected JSON value to be an 'object' but it is of type '%s'",
            value.type_name());

    return value.get_ref<const nlohmann::json::object_t &>();
}

const nlohmann::json::array_t & getArray(const nlohmann::json & value)
{
    if (!value.is_array())
        throw Error(
            "Expected JSON value to be an 'array' but it is of type '%s'",
            value.type_name());

    return value.get_ref<const nlohmann::json::array_t &>();
}

const nlohmann::json::string_t & getString(const nlohmann::json & value)
{
    if (!value.is_string())
        throw Error(
            "Expected JSON value to be a 'string' but it is of type '%s'",
            value.type_name());

    return value.get_ref<const nlohmann::json::string_t &>();
}

const nlohmann::json::number_integer_t & getInteger(const nlohmann::json & value)
{
    if (!value.is_number_integer())
        throw Error(
            "Expected JSON value to be an 'integer' but it is of type '%s'",
            value.type_name());

    return value.get_ref<const nlohmann::json::number_integer_t &>();
}

const nlohmann::json::boolean_t & getBoolean(const nlohmann::json & value)
{
    if (!value.is_boolean())
        throw Error(
            "Expected JSON value to be a 'boolean' but it is of type '%s'",
            value.type_name());

    return value.get_ref<const nlohmann::json::boolean_t &>();
}

Strings getStringList(const nlohmann::json & value)
{
    auto jsonArray = getArray(value);

    Strings stringList;

    for (const auto & elem: jsonArray)
        stringList.push_back(elem);

    return stringList;
}

StringMap getStringMap(const nlohmann::json & value)
{
    auto jsonArray = getObject(value);

    StringMap stringMap;

    for (const auto & [key, value]: jsonArray)
        stringMap[getString(key)] = getString(value);

    return stringMap;
}

StringSet getStringSet(const nlohmann::json & value)
{
    auto jsonArray = getArray(value);

    StringSet stringSet;

    for (const auto & elem: jsonArray)
        stringSet.insert(getString(elem));

    return stringSet;
}
}
