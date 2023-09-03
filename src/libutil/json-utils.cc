#include "json-utils.hh"
#include "error.hh"

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
}
